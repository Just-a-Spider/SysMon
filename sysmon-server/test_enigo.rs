use enigo::{Enigo, Keyboard, Settings, Key, Direction};
fn main() {
    let mut enigo = Enigo::new(&Settings::default()).unwrap();
    enigo.key(Key::Return, Direction::Click).unwrap();
    enigo.text("Hello").unwrap();
}
