# Menu

## Purpose
An LCD-driven navigable menu: a list of selectable `MenuItem`s rendered on a
`LiquidCrystal` display, with per-item action callbacks.

## Key API
- `class MenuItem` — one menu entry.
  - `MenuItem(String menuText)` / `MenuItem(String menuText, String selectedText)` /
    `MenuItem(String menuText, String selectedText, menu_action_t action, void *args)`
  - `void select(boolean selected)`, `int action(void)`
- `class Menu` — the menu controller.
  - `Menu(int numItems, MenuItem **items, LiquidCrystal *lcd)`
  - `void next(void)` / `void prev(void)` — move the cursor.
  - `void enter(void)` — select/deselect the current item.
  - `int action(void)` — invoke the current item's `menu_action_t` callback.
  - `void display(void)` — render the current state to the LCD.

## Configuration
None.

## Dependencies
- External: Arduino `LiquidCrystal` (a `LiquidCrystal*` is passed to the `Menu`
  constructor). Note: it drives `LiquidCrystal` directly, not the sibling `LCD` library.

## Example
No standalone example sketch.

---
Part of [ArduinoLibs](../README.md).
