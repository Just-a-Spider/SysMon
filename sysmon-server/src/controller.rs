#[cfg(target_os = "linux")]
use evdev::uinput::VirtualDevice;
#[cfg(target_os = "linux")]
use evdev::{
    AbsInfo, AbsoluteAxisCode, AttributeSet, BusType, InputEvent, InputId, KeyCode,
    UinputAbsSetup,
};
use std::sync::atomic::{AtomicBool, AtomicU32, AtomicU64, Ordering};
use std::sync::Arc;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};
use tokio::net::UdpSocket;
#[cfg(target_os = "linux")]
use tokio::sync::Mutex;

pub const MAGIC: &[u8; 4] = b"3PAD";
pub const PACKET_SIZE: usize = 28;

static LAST_AUTH_WARN: AtomicU64 = AtomicU64::new(0);

#[derive(Debug, PartialEq, Eq, Clone)]
pub struct ControllerPacket {
    pub seq: u32,
    pub pin: u32,
    pub buttons: u32,
    pub circle_x: i16,
    pub circle_y: i16,
    pub right_x: i16,
    pub right_y: i16,
    pub flags: u32,
}

pub fn parse_controller_packet(buf: &[u8], expected_pin: u32) -> Option<ControllerPacket> {
    if buf.len() < PACKET_SIZE {
        return None;
    }
    if &buf[0..4] != MAGIC {
        return None;
    }

    let seq = u32::from_be_bytes([buf[4], buf[5], buf[6], buf[7]]);
    let packet_pin = u32::from_be_bytes([buf[8], buf[9], buf[10], buf[11]]);

    // Lightweight PIN authentication (<1ns check)
    if expected_pin != 0 && packet_pin != expected_pin {
        let now_secs = SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default().as_secs();
        let prev = LAST_AUTH_WARN.swap(now_secs, Ordering::Relaxed);
        if now_secs.saturating_sub(prev) >= 2 {
            eprintln!("[SysMon Controller] Auth failed: received PIN {}, expected PIN {}", packet_pin, expected_pin);
        }
        return None;
    }

    let buttons = u32::from_be_bytes([buf[12], buf[13], buf[14], buf[15]]);
    let circle_x = i16::from_be_bytes([buf[16], buf[17]]);
    let circle_y = i16::from_be_bytes([buf[18], buf[19]]);
    let right_x = i16::from_be_bytes([buf[20], buf[21]]);
    let right_y = i16::from_be_bytes([buf[22], buf[23]]);
    let flags = u32::from_be_bytes([buf[24], buf[25], buf[26], buf[27]]);

    Some(ControllerPacket {
        seq,
        pin: packet_pin,
        buttons,
        circle_x,
        circle_y,
        right_x,
        right_y,
        flags,
    })
}

pub fn should_process_packet(seq: u32, last_seq: u32, is_active_session: bool) -> bool {
    // Fresh restart: If session was inactive or sequence reset to beginning (<= 5)
    if !is_active_session || seq <= 5 {
        return true;
    }
    // Normal in-order packet
    if seq >= last_seq {
        return true;
    }
    // Drop late / reordered packet if within short jitter window (e.g. 10000 packets)
    // and not a sequence wrap-around
    if (last_seq - seq) < 10000 {
        return false;
    }
    true
}

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

    #[cfg(target_os = "linux")]
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
    #[cfg(target_os = "linux")]
    if let Some(dev_watchdog) = device.clone() {
        let last_time_clone = last_packet_time.clone();
        let active_state_clone = has_active_state.clone();
        let last_seq_clone = last_seq.clone();
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
                    last_seq_clone.store(0, Ordering::SeqCst);
                }
            }
        });
    }

    let mut buf = [0u8; 64];

    loop {
        match socket.recv_from(&mut buf).await {
            Ok((len, _peer)) => {
                let packet = match parse_controller_packet(&buf[..len], pin) {
                    Some(p) => p,
                    None => continue,
                };

                let prev_seq = last_seq.load(Ordering::SeqCst);
                let is_active = has_active_state.load(Ordering::SeqCst);
                if !should_process_packet(packet.seq, prev_seq, is_active) {
                    continue;
                }
                last_seq.store(packet.seq, Ordering::SeqCst);

                {
                    let mut lock = last_packet_time.lock().await;
                    *lock = Instant::now();
                }
                has_active_state.store(true, Ordering::SeqCst);

                #[cfg(target_os = "linux")]
                if let Some(ref dev_arc) = device {
                    let mut dev = dev_arc.lock().await;
                    apply_gamepad_state(
                        &mut dev,
                        packet.buttons,
                        packet.circle_x,
                        packet.circle_y,
                        packet.right_x,
                        packet.right_y,
                        packet.flags,
                    );
                }

                #[cfg(not(target_os = "linux"))]
                {
                    let _ = packet;
                }
            }
            Err(e) => {
                eprintln!("UDP controller recv error: {}", e);
                tokio::time::sleep(Duration::from_millis(10)).await;
            }
        }
    }
}

#[cfg(target_os = "linux")]
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

#[cfg(target_os = "linux")]
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

    let (btn_south, btn_east, btn_left, btn_top) = if is_physical_map {
        (
            (buttons & (1 << 1)) != 0,  // 3DS B -> South (Bottom / Steam A)
            (buttons & (1 << 0)) != 0,  // 3DS A -> East (Right / Steam B)
            (buttons & (1 << 11)) != 0, // 3DS Y -> Left (Steam X)
            (buttons & (1 << 10)) != 0, // 3DS X -> Top (Steam Y)
        )
    } else {
        (
            (buttons & (1 << 0)) != 0,  // 3DS A -> Letter A (Steam A)
            (buttons & (1 << 1)) != 0,  // 3DS B -> Letter B (Steam B)
            (buttons & (1 << 10)) != 0, // 3DS X -> Letter X (Steam X)
            (buttons & (1 << 11)) != 0, // 3DS Y -> Letter Y (Steam Y)
        )
    };

    events.push(InputEvent::new(evdev::EventType::KEY.0, KeyCode::BTN_SOUTH.0, if btn_south { 1 } else { 0 }));
    events.push(InputEvent::new(evdev::EventType::KEY.0, KeyCode::BTN_EAST.0,  if btn_east  { 1 } else { 0 }));
    events.push(InputEvent::new(evdev::EventType::KEY.0, KeyCode::BTN_NORTH.0, if btn_left  { 1 } else { 0 })); // 0x133 -> Steam b2 (Steam X / Left)
    events.push(InputEvent::new(evdev::EventType::KEY.0, KeyCode::BTN_WEST.0,  if btn_top   { 1 } else { 0 })); // 0x134 -> Steam b3 (Steam Y / Top)

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

#[cfg(target_os = "linux")]
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

#[cfg(test)]
mod tests {
    use super::*;

    fn build_raw_packet(seq: u32, pin: u32, buttons: u32, cx: i16, cy: i16, rx: i16, ry: i16, flags: u32) -> Vec<u8> {
        let mut buf = Vec::with_capacity(PACKET_SIZE);
        buf.extend_from_slice(b"3PAD");
        buf.extend_from_slice(&seq.to_be_bytes());
        buf.extend_from_slice(&pin.to_be_bytes());
        buf.extend_from_slice(&buttons.to_be_bytes());
        buf.extend_from_slice(&cx.to_be_bytes());
        buf.extend_from_slice(&cy.to_be_bytes());
        buf.extend_from_slice(&rx.to_be_bytes());
        buf.extend_from_slice(&ry.to_be_bytes());
        buf.extend_from_slice(&flags.to_be_bytes());
        buf
    }

    #[test]
    fn test_parse_valid_packet() {
        let raw = build_raw_packet(42, 1234, 0x03, 1000, -2000, 500, -500, 0x04);
        let parsed = parse_controller_packet(&raw, 1234).expect("Packet should parse successfully");
        assert_eq!(parsed.seq, 42);
        assert_eq!(parsed.pin, 1234);
        assert_eq!(parsed.buttons, 0x03);
        assert_eq!(parsed.circle_x, 1000);
        assert_eq!(parsed.circle_y, -2000);
        assert_eq!(parsed.right_x, 500);
        assert_eq!(parsed.right_y, -500);
        assert_eq!(parsed.flags, 0x04);
    }

    #[test]
    fn test_parse_invalid_magic() {
        let mut raw = build_raw_packet(1, 1234, 0, 0, 0, 0, 0, 0);
        raw[0] = b'X';
        assert!(parse_controller_packet(&raw, 1234).is_none());
    }

    #[test]
    fn test_parse_wrong_pin() {
        let raw = build_raw_packet(1, 9999, 0, 0, 0, 0, 0, 0);
        assert!(parse_controller_packet(&raw, 1234).is_none());
    }

    #[test]
    fn test_parse_zero_pin_allows_any() {
        let raw = build_raw_packet(1, 9999, 0, 0, 0, 0, 0, 0);
        assert!(parse_controller_packet(&raw, 0).is_some());
    }

    #[test]
    fn test_parse_short_packet() {
        let raw = vec![0u8; 10];
        assert!(parse_controller_packet(&raw, 1234).is_none());
    }

    #[test]
    fn test_sequence_tracking_session_restart() {
        // When previous session left at seq 500, but new session starts at seq 1:
        assert!(should_process_packet(1, 500, true));
        assert!(should_process_packet(2, 500, true));
        assert!(should_process_packet(5, 500, true));
        
        // When session was inactive (watchdog triggered, active_session = false):
        assert!(should_process_packet(6, 500, false));
    }

    #[test]
    fn test_sequence_tracking_in_order_and_jitter() {
        // In-order packets
        assert!(should_process_packet(10, 9, true));
        assert!(should_process_packet(11, 10, true));

        // Out-of-order late packet within active session
        assert!(!should_process_packet(8, 10, true));
        assert!(!should_process_packet(9, 10, true));
    }
}

