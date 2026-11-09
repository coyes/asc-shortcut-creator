# asc — A Shortcut Creator

Create menu shortcuts for Windows games, Linux applications, and AppImages.

## Get started

Install `make`, `gcc`, and the GTK 4 development files for your distribution. To create shortcuts for Windows games, install [umu-launcher](https://github.com/Open-Wine-Components/umu-launcher).

```bash
git clone https://github.com/coyes/asc-shortcut-creator.git
cd asc-shortcut-creator
make
./asc
```

To install the app in `~/.local/bin`, run `make install` and then launch it with `asc`.

## License

The project is licensed under the [MIT License](LICENSE). The bundled `stb_image` headers include their own MIT or public-domain license options.

## Notes

- Should work on any DE that respects the XDG desktop spec
- **Disclaimer**: This project was written with AI (Codex) and tested on Fedora 44 and openSUSE Leap 16.1 with KDE on both.
