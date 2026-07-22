use sysinfo::Components;

fn main() {
    let components = Components::new_with_sys_info();
    for component in &components {
        println!("{:?} - {} C", component.label(), component.temperature());
    }
}
