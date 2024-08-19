# PLF documentation

**This is windows only, if you want to run it on something else then you can write a port**

## To install:

1. Get the rom generator from [imagRom](https://github.com/GroceyLot/ImageROM/releases/tag/Release)
2. Install plf via the release or the source

## Usage:

plf <script_path> <rom_path>
imagRom <action : (encode, decode)>
  encode - <images_folder_path> <rom_path>
  decode - <rom_path> <images_folder_path>

## Documentation:

### Libraries:

#### `color`:
```lua
color.rgb(r, g, b) -- Takes in r, g and b as integers from 0 to 7, and returns a color as an integer to be used by the program
color.hsv(h, s, v) -- Literally the same as above but with h, s, v
color.greyscale(input) -- Returns the closest grey to the input color
```

#### `drawing`:
```lua
drawing.rect(image, x, y) -- Draws the image with top left corner at x, y
drawing.shader(function(x, y)
  return color.rgb(math.random(0, 7), 0, 0)
end) -- Draws the function to the full screen
```

#### `texture`:
```lua
texture.fromShader(function (x, y)
  return 1
end, width, height) -- Runs the shader to output a texture with width and height
texture.fromRom(id) -- Takes the texture from the rom with the id (id is a 4 letter string being first 4 of the image name)
```

#### `mouse`:
```lua
mouse.position() -- Returns mouse x and y as an integer based on the screen
mouse.down(button) -- Returns a boolean based on button (button is 1, 2 or 3)
mouse.visible(visible) -- Changes the mouse visibility
```

#### `keyboard`:
```lua
keyboard.down(keycode)
--[[ Keycode is either a 1 letter string being the letter or space, or it's a longer string which can be any of these:
enter, shift, control, alt, escape, back, tab, left, right, up, down
]]
```

#### `window`:
```lua
window.title(title) -- Changes the window title
window.close() -- Stops the game
window.fullscreen(fullscreen) -- Enables or disables fullscreen
window.message(text) -- Shows a popup message box with text
```

#### `util`:
```lua
util.distance(x1, y1, x2, y2) -- Returns the distance between 2 2d points
util.clamp(value, min, max) -- Clamps a value
util.random(max) -- Random number up to max
util.random(min, max) -- Random number from min to max
util.lerp(start, end, t) -- Lerps from start to end with time t
```

### Required globals:
```lua
width = 320
height = 180
title = "Window title"
dt = 16 -- Target update ms

function load() end
function update() end
function down(button) end
function up(button) end
function close() end
```
