use evdev::uinput::VirtualDevice;
use evdev::{
    AbsInfo, AbsoluteAxisCode, AttributeSet, BusType, InputEvent, InputId, KeyCode,
    UinputAbsSetup,
};
use std::sync::atomic::{AtomicBool, AtomicU32, Ordering};
use std::sync::Arc;
use std::time::{Duration, Instant};
use tokio::net::UdpSocket;
use tokio::sync::Mutex;

const MAGIC: &[u8; 4] = b"3PAD";
const PACKET_SIZE: usize = 28;

pub async fn run_controller_server(port: u16, pin: u32) {
    let addr = format!("0.0.0.0:{}", port);
    let socket = match UdpSocket::bind(&addr).await {
        Ok(s) => {
            println!("UDP Controller server listening on {}", addr);
            Arc::new(s)
        }
        Err(e) => {
            eprintln!("Failed to bind UDP Controller socket on {}: {}", addr, e);
            return;
        }
    };

    // Attempt to create Linux uinput virtual gamepad
    let device = match create_virtual_gamepad() {
        Ok(dev) => {
            println!("Initialized SysMon 3DS Virtual Controller on /dev/uinput");
            Some(Arc::new(Mutex::new(dev)))
        }
        Err(e) => {
            eprintln!(
                "Warning: Could not create /dev/uinput virtual gamepad: {}. (Ensure user has write permissions to /dev/uinput or udev rule is installed)",
                e
            );
            None
        }
    };

    let last_packet_time = Arc::new(tokio::sync::Mutex::new(Instant::now()));
    let last_seq = Arc::new(AtomicU32::new(0));
    let has_active_state = Arc::new(AtomicBool::new(false));

    // Watchdog Task: Zeroes out axes and releases buttons if no packet received for >200ms
    if let Some(dev_watchdog) = device.clone() {
        let last_time_clone = last_packet_time.clone();
        let active_state_clone = has_active_state.clone();
        tokio::spawn(async move {
            let mut interval = tokio::time::interval(Duration::from_millis(50));
            loop {
                interval.tick().await;
                let elapsed = {
                    let lock = last_time_clone.lock().await;
                    lock.elapsed()
                };

                if elapsed > Duration::from_millis(200) && active_state_clone.load(Ordering::SeqCst) {
                    let mut dev = dev_watchdog.lock().await;
                    zero_virtual_gamepad(&mut dev);
                    active_state_clone.store(false, Ordering::SeqCst);
                }
            }
        });
    }

    let mut buf = [0u8; 64];

    loop {
        match socket.recv_from(&mut buf).await {
            Ok((len, _peer)) => {
                if len < PACKET_SIZE {
                    continue;
                }

                if &buf[0..4] != MAGIC {
                    continue;
                }

                let seq = u32::from_be_bytes([buf[4], buf[5], buf[6], buf[7]]);
                let packet_pin = u32::from_be_bytes([buf[8], buf[9], buf[10], buf[11]]);

                // Lightweight PIN authentication (<1ns check)
                if pin != 0 && packet_pin != pin {
                    continue;
                }

                // Monotonic sequence tracking (drop late / reordered UDP packets)
                let prev_seq = last_seq.load(Ordering::SeqCst);
                if seq < prev_seq && (prev_seq - seq) < 10000 {
                    continue;
                }
                last_seq.store(seq, Ordering::SeqCst);

                let buttons = u32::from_be_bytes([buf[12], buf[13], buf[14], buf[15]]);
                let circle_x = i16::from_be_bytes([buf[16], buf[17]]);
                let circle_y = i16::from_be_bytes([buf[18], buf[19]]);
                let right_x = i16::from_be_bytes([buf[20], buf[21]]);
                let right_y = i16::from_be_bytes([buf[22], buf[23]]);
                let flags = u32::from_be_bytes([buf[24], buf[25], buf[26], buf[27]]);

                {
                    let mut lock = last_packet_time.lock().await;
                    *lock = Instant::now();
                }
                has_active_state.store(true, Ordering::SeqCst);

                if let Some(ref dev_arc) = device {
                    let mut dev = dev_arc.lock().await;
                    apply_gamepad_state(&mut dev, buttons, circle_x, circle_y, right_x, right_y, flags);
                }
            }
            Err(e) => {
                eprintln!("UDP controller recv error: {}", e);
                tokio::time::sleep(Duration::from_millis(10)).await;
            }
        }
    }
}

fn create_virtual_gamepad() -> std::io::Result<evdev::uinput::VirtualDevice> {
    let mut keys = AttributeSet::<KeyCode>::new();
    keys.insert(KeyCode::BTN_SOUTH); // 3DS B (Physical South / PSP Cross)
    keys.insert(KeyCode::BTN_EAST);  // 3DS A (Physical East / PSP Circle)
    keys.insert(KeyCode::BTN_WEST);  // 3DS Y (Physical West / PSP Square)
    keys.insert(KeyCode::BTN_NORTH); // 3DS X (Physical North / PSP Triangle)
    keys.insert(KeyCode::BTN_TL);     // L
    keys.insert(KeyCode::BTN_TR);     // R
    keys.insert(KeyCode::BTN_TL2);    // ZL
    keys.insert(KeyCode::BTN_TR2);    // ZR
    keys.insert(KeyCode::BTN_SELECT); // SELECT
    keys.insert(KeyCode::BTN_START);  // START
    keys.insert(KeyCode::BTN_MODE);   // HOME

    let abs_stick = AbsInfo::new(0, -32767, 32767, 16, 128, 0);
    let abs_hat = AbsInfo::new(0, -1, 1, 0, 0, 0);

    let x_setup = UinputAbsSetup::new(AbsoluteAxisCode::ABS_X, abs_stick);
    let y_setup = UinputAbsSetup::new(AbsoluteAxisCode::ABS_Y, abs_stick);
    let rx_setup = UinputAbsSetup::new(AbsoluteAxisCode::ABS_RX, abs_stick);
    let ry_setup = UinputAbsSetup::new(AbsoluteAxisCode::ABS_RY, abs_stick);
    let hat0x_setup = UinputAbsSetup::new(AbsoluteAxisCode::ABS_HAT0X, abs_hat);
    let hat0y_setup = UinputAbsSetup::new(AbsoluteAxisCode::ABS_HAT0Y, abs_hat);

    let dev = VirtualDevice::builder()?
        .name("SysMon 3DS Virtual Controller")
        .input_id(InputId::new(BusType::BUS_USB, 0x057e, 0x0306, 1)) // Nintendo vendor ID tag
        .with_keys(&keys)?
        .with_absolute_axis(&x_setup)?
        .with_absolute_axis(&y_setup)?
        .with_absolute_axis(&rx_setup)?
        .with_absolute_axis(&ry_setup)?
        .with_absolute_axis(&hat0x_setup)?
        .with_absolute_axis(&hat0y_setup)?
        .build()?;

    Ok(dev)
}

fn apply_gamepad_state(
    dev: &mut evdev::uinput::VirtualDevice,
    buttons: u32,
    circle_x: i16,
    circle_y: i16,
    right_x: i16,
    right_y: i16,
    flags: u32,
) {
    let mut events = Vec::with_capacity(16);

    // 3DS Hardware Key bitmask:
    // bit 0: A (East), bit 1: B (South), bit 2: Select, bit 3: Start
    // bit 4: D-Right, bit 5: D-Left, bit 6: D-Up, bit 7: D-Down
    // bit 8: R, bit 9: L, bit 10: X (North), bit 11: Y (West)
    // bit 14: ZL, bit 15: ZR

    // Physical Position Mapping (Default for PSP / standard controllers):
    // 3DS B (South) -> BTN_SOUTH (Cross / A)
    // 3DS A (East)  -> BTN_EAST  (Circle / B)
    // 3DS Y (West)  -> BTN_WEST  (Square / X)
    // 3DS X (North) -> BTN_NORTH (Triangle / Y)
    let is_physical_map = (flags & 0x04) != 0 || true;

    let (btn_south, btn_east, btn_west, btn_north) = if is_physical_map {
        (
            (buttons & (1 << 1)) != 0, // 3DS B -> South
            (buttons & (1 << 0)) != 0, // 3DS A -> East
            (buttons & (1 << 11)) != 0, // 3DS Y -> West
            (buttons & (1 << 10)) != 0, // 3DS X -> North
        )
    } else {
        (
            (buttons & (1 << 0)) != 0, // 3DS A -> South
            (buttons & (1 << 1)) != 0, // 3DS B -> East
            (buttons & (1 << 10)) != 0, // 3DS X -> West
            (buttons & (1 << 11)) != 0, // 3DS Y -> North
        )
    };

    events.push(InputEvent::new(evdev::EventType::KEY.0, KeyCode::BTN_SOUTH.0, if btn_south { 1 } else { 0 }));
    events.push(InputEvent::new(evdev::EventType::KEY.0, KeyCode::BTN_EAST.0, if btn_east { 1 } else { 0 }));
    events.push(InputEvent::new(evdev::EventType::KEY.0, KeyCode::BTN_WEST.0, if btn_west { 1 } else { 0 }));
    events.push(InputEvent::new(evdev::EventType::KEY.0, KeyCode::BTN_NORTH.0, if btn_north { 1 } else { 0 }));

    events.push(InputEvent::new(evdev::EventType::KEY.0, KeyCode::BTN_TL.0, if (buttons & (1 << 9)) != 0 { 1 } else { 0 }));
    events.push(InputEvent::new(evdev::EventType::KEY.0, KeyCode::BTN_TR.0, if (buttons & (1 << 8)) != 0 { 1 } else { 0 }));
    events.push(InputEvent::new(evdev::EventType::KEY.0, KeyCode::BTN_TL2.0, if (buttons & (1 << 14)) != 0 { 1 } else { 0 }));
    events.push(InputEvent::new(evdev::EventType::KEY.0, KeyCode::BTN_TR2.0, if (buttons & (1 << 15)) != 0 { 1 } else { 0 }));
    events.push(InputEvent::new(evdev::EventType::KEY.0, KeyCode::BTN_SELECT.0, if (buttons & (1 << 2)) != 0 { 1 } else { 0 }));
    events.push(InputEvent::new(evdev::EventType::KEY.0, KeyCode::BTN_START.0, if (buttons & (1 << 3)) != 0 { 1 } else { 0 }));

    // D-Pad -> ABS_HAT0X and ABS_HAT0Y
    let hat_x: i32 = if (buttons & (1 << 4)) != 0 { 1 } else if (buttons & (1 << 5)) != 0 { -1 } else { 0 };
    let hat_y: i32 = if (buttons & (1 << 7)) != 0 { 1 } else if (buttons & (1 << 6)) != 0 { -1 } else { 0 };
    events.push(InputEvent::new(evdev::EventType::ABSOLUTE.0, AbsoluteAxisCode::ABS_HAT0X.0, hat_x));
    events.push(InputEvent::new(evdev::EventType::ABSOLUTE.0, AbsoluteAxisCode::ABS_HAT0Y.0, hat_y));

    // Left Stick / Circle Pad (Invert Y for evdev standard: up is negative)
    events.push(InputEvent::new(evdev::EventType::ABSOLUTE.0, AbsoluteAxisCode::ABS_X.0, circle_x as i32));
    events.push(InputEvent::new(evdev::EventType::ABSOLUTE.0, AbsoluteAxisCode::ABS_Y.0, -(circle_y as i32)));

    // Right Stick: C-Stick takes precedence over Touch Stick
    let (final_rx, final_ry) = if (flags & 0x01) != 0 || (flags & 0x02) != 0 {
        (right_x as i32, -(right_y as i32))
    } else {
        (0, 0)
    };
    events.push(InputEvent::new(evdev::EventType::ABSOLUTE.0, AbsoluteAxisCode::ABS_RX.0, final_rx));
    events.push(InputEvent::new(evdev::EventType::ABSOLUTE.0, AbsoluteAxisCode::ABS_RY.0, final_ry));

    let _ = dev.emit(&events);
}

fn zero_virtual_gamepad(dev: &mut evdev::uinput::VirtualDevice) {
    let mut events = Vec::with_capacity(16);
    events.push(InputEvent::new(evdev::EventType::KEY.0, KeyCode::BTN_SOUTH.0, 0));
    events.push(InputEvent::new(evdev::EventType::KEY.0, KeyCode::BTN_EAST.0, 0));
    events.push(InputEvent::new(evdev::EventType::KEY.0, KeyCode::BTN_WEST.0, 0));
    events.push(InputEvent::new(evdev::EventType::KEY.0, KeyCode::BTN_NORTH.0, 0));
    events.push(InputEvent::new(evdev::EventType::KEY.0, KeyCode::BTN_TL.0, 0));
    events.push(InputEvent::new(evdev::EventType::KEY.0, KeyCode::BTN_TR.0, 0));
    events.push(InputEvent::new(evdev::EventType::KEY.0, KeyCode::BTN_TL2.0, 0));
    events.push(InputEvent::new(evdev::EventType::KEY.0, KeyCode::BTN_TR2.0, 0));
    events.push(InputEvent::new(evdev::EventType::KEY.0, KeyCode::BTN_SELECT.0, 0));
    events.push(InputEvent::new(evdev::EventType::KEY.0, KeyCode::BTN_START.0, 0));
    events.push(InputEvent::new(evdev::EventType::ABSOLUTE.0, AbsoluteAxisCode::ABS_HAT0X.0, 0));
    events.push(InputEvent::new(evdev::EventType::ABSOLUTE.0, AbsoluteAxisCode::ABS_HAT0Y.0, 0));
    events.push(InputEvent::new(evdev::EventType::ABSOLUTE.0, AbsoluteAxisCode::ABS_X.0, 0));
    events.push(InputEvent::new(evdev::EventType::ABSOLUTE.0, AbsoluteAxisCode::ABS_Y.0, 0));
    events.push(InputEvent::new(evdev::EventType::ABSOLUTE.0, AbsoluteAxisCode::ABS_RX.0, 0));
    events.push(InputEvent::new(evdev::EventType::ABSOLUTE.0, AbsoluteAxisCode::ABS_RY.0, 0));

    let _ = dev.emit(&events);
}
