# Development

## C style

Follow the [GNOME C Coding Style](https://developer.gnome.org/documentation/guidelines/programming/coding-style.html), except where this project has an established convention:

- Indent with four spaces. Keep the existing style when editing nearby code.
- Use GLib cleanup attributes such as `g_autoptr` and `g_autofree` for local resources when they make ownership clearer.

Keep changes focused; do not reformat unrelated code. Consult GNOME's guide for other C style and memory-management guidance.

## Build

Build with `make`. See the [README](README.md) for dependencies, setup, and run instructions.
