# Fynder

Fynder es un administrador de archivos para Linux Mint (Cinnamon) con la
manera de trabajar del Finder de macOS Catalina. Es un fork de
[Nemo](https://github.com/linuxmint/nemo), el administrador de archivos de
Linux Mint, y se instala al lado del Nemo del sistema sin tocarlo.

Forma parte de [mint-macos](https://github.com/josemalena/mint-macos), que
lleva a un usuario de macOS Catalina a Linux Mint.

## Qué cambia respecto a Nemo

- **Barra de herramientas como la del Finder:** atrás y adelante, las vistas
  (íconos, lista, columnas y compacta), Arrange, la acción ⚙ y una búsqueda
  que se encoge en ventanas angostas; lo que no cabe va al menú «»».
- **Lateral como el del Finder:** secciones Favorites y Locations con
  encabezados que se esconden (Hide/Show), y el lateral que se oculta solo
  cuando la ventana es muy angosta. El disco del sistema se llama
  «Macintosh HD» (se puede cambiar).
- **Lista como la del Finder:** triángulos para desplegar carpetas en su
  sitio, filas alternas y Arrange por columna.
- **Menús y atajos del Finder:** los menús File, Edit, View, Go y Window, el
  menú contextual de la selección y los atajos con ⌘ en su orden de macOS.
  Quick Look con ⌘Y (por nemo-preview).
- **Menú global:** con la opción `global-menu`, publica su barra de menús por
  D-Bus (GMenuModel) para que el panel la pinte arriba, como en macOS,
  incluido el menú propio de la app (About, Preferences…, Empty Trash…).
- **Preferencias como las del Finder** (General, Sidebar, Advanced) y View
  Options (⌘J).
- **Identidad propia:** ejecutable `fynder`, app id
  `io.github.josemalena.Fynder`, ajustes en `io.github.josemalena.fynder.*`,
  datos en `~/.local/share/fynder` y configuración en `~/.config/fynder`.

El aspecto (colores, íconos, fuentes) no viene aquí: lo pone el tema
Constanza de mint-macos. Fynder no trae imágenes, íconos ni fuentes de Apple;
el tema los genera en la máquina del usuario a partir de su propia Mac.

## Compilar e instalar (en ~/.local, sin sudo)

Paquetes de compilación en Linux Mint 22 (una vez, con sudo):

    sudo apt install build-essential pkg-config meson ninja-build gettext \
      gobject-introspection libgirepository1.0-dev libglib2.0-dev libgtk-3-dev \
      libatk1.0-dev libpango1.0-dev libjson-glib-dev libcinnamon-desktop-dev \
      libgail-3-dev libx11-dev libxapp-dev libexif-dev libexempi-dev nemo-preview

Después, como el usuario:

    meson setup build --prefix="$HOME/.local"
    ninja -C build
    ninja -C build install
    glib-compile-schemas "$HOME/.local/share/glib-2.0/schemas"

El prefijo va compilado dentro del binario: tiene que ser el mismo donde se
instala. Si cambia el esquema, hay que recompilarlo en el mismo paso, o la
aplicación no arranca.

Para que sea el administrador de archivos por defecto:

    xdg-mime default fynder.desktop inode/directory application/x-gnome-saved-search

Quien venía de la versión anterior (nemo-mac) pasa sus ajustes con
`utils/migrar-ajustes-a-fynder.sh` (respalda y no pisa nada sin `--forzar`).

## Licencia

Igual que Nemo: GPL versión 2 o posterior (ver `COPYING`); la biblioteca de
extensiones, LGPL (ver `COPYING.LIB` y `COPYING.EXTENSIONS`). El código de
Nemo es de Linux Mint y de los autores de Nautilus que lista `AUTHORS`.
