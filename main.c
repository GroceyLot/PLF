#include "SDL.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

// Global variables
SDL_Window *window = NULL;
SDL_Renderer *renderer = NULL;
SDL_Texture *texture = NULL;
// Implement double buffering
Uint32 *pixelsFront = NULL;
Uint32 *pixelsBack = NULL;
int bufferWidth = 0, bufferHeight = 0;
// Removed: double dt = 0.0; // Target frame duration in seconds
lua_State *L = NULL;
bool running = true;
bool isFullscreen = false;
const char *romPathGlobal = NULL;

// Suppress flag
bool suppress = false;

// Define global pixel format for color mapping
SDL_PixelFormat *globalFormat = NULL;

#define LOG(fmt, ...)                                                           \
    do                                                                          \
    {                                                                           \
        if (!suppress)                                                          \
            fprintf(stderr, "[%s:%d] " fmt, __FILE__, __LINE__, ##__VA_ARGS__); \
    } while (0)
#define PRINT(fmt, ...)                 \
    do                                  \
    {                                   \
        if (!suppress)                  \
            printf(fmt, ##__VA_ARGS__); \
    } while (0)

// Function declarations
void InitializeLua(const char *scriptPath);
void SetupBuffers(int width, int height);
void UpdatePixelsFromLua(double deltaTime); // Changed parameter name
void DrawBuffer();
int color_rgb(lua_State *L);
int color_hsv(lua_State *L);
int color_greyscale(lua_State *L);
int texture_fromShader(lua_State *L);
int texture_fromRom(lua_State *L);
int drawing_shader(lua_State *L);
int drawing_rect(lua_State *L);
int drawing_circle(lua_State *L);
int drawing_line(lua_State *L);
int drawing_pixel(lua_State *L);
int mouse_position(lua_State *L);
int mouse_down(lua_State *L);
int mouse_center(lua_State *L);
int mouse_visible(lua_State *L);
int keyboard_down(lua_State *L);
int window_title(lua_State *L);
int window_close(lua_State *L);
int window_fullscreen(lua_State *L);
int window_message(lua_State *L);
int http_get(lua_State *L);
int util_distance(lua_State *L);
int util_random(lua_State *L);
int util_clamp(lua_State *L);
int util_lerp(lua_State *L);
int util_intersect(lua_State *L);

// Helper functions
int EncodeColor(int rIndex, int gIndex, int bIndex);
Uint32 DecodeColor(int encodedColor);

// Custom function to check if a number is an integer
int lua_isinteger_custom(lua_State *L, int idx)
{
    if (!lua_isnumber(L, idx))
        return 0;
    lua_Number n = lua_tonumber(L, idx);
    lua_Integer i = lua_tointeger(L, idx);
    return n == (lua_Number)i;
}

int EncodeColor(int rIndex, int gIndex, int bIndex)
{
    // Ensure indices are within 0-7
    if (rIndex < 0 || rIndex > 7 || gIndex < 0 || gIndex > 7 || bIndex < 0 || bIndex > 7)
    {
        LOG("RGB indices must be between 0 and 7\n");
        return 1; // Default to black if out of range
    }

    return (rIndex * 64) + (gIndex * 8) + bIndex + 1;
}

Uint32 DecodeColor(int encodedColor)
{
    if (encodedColor < 1 || encodedColor > 512)
    {
        LOG("Encoded color value out of range: %d\n", encodedColor);
        return SDL_MapRGBA(globalFormat, 0, 0, 0, 255); // Default to black
    }

    int encodedValue = encodedColor - 1;
    int rIndex = encodedValue / 64;
    int gIndex = (encodedValue % 64) / 8;
    int bIndex = encodedValue % 8;

    Uint8 r = rIndex * 36;
    Uint8 g = gIndex * 36;
    Uint8 b = bIndex * 36;
    Uint8 a = 255;

    return SDL_MapRGBA(globalFormat, r, g, b, a);
}

// Initialize Lua and register functions
void InitializeLua(const char *scriptPath)
{
    L = luaL_newstate();
    luaL_openlibs(L);

    // Register color library
    luaL_Reg colorLib[] = {
        {"rgb", color_rgb},
        {"hsv", color_hsv},
        {"greyscale", color_greyscale},
        {NULL, NULL}};
    luaL_newlib(L, colorLib);
    lua_setglobal(L, "color");

    // Register drawing library
    luaL_Reg drawingLib[] = {
        {"shader", drawing_shader},
        {"rect", drawing_rect},
        {"circle", drawing_circle},
        {"line", drawing_line},
        {"pixel", drawing_pixel},
        {NULL, NULL}};
    luaL_newlib(L, drawingLib);
    lua_setglobal(L, "drawing");

    // Register texture library
    luaL_Reg textureLib[] = {
        {"fromShader", texture_fromShader},
        {"fromRom", texture_fromRom},
        {NULL, NULL}};
    luaL_newlib(L, textureLib);
    lua_setglobal(L, "texture");

    // Register mouse library
    luaL_Reg mouseLib[] = {
        {"position", mouse_position},
        {"down", mouse_down},
        {"visible", mouse_visible},
        {"center", mouse_center},
        {NULL, NULL}};
    luaL_newlib(L, mouseLib);
    lua_setglobal(L, "mouse");

    // Register keyboard library
    luaL_Reg keyboardLib[] = {
        {"down", keyboard_down},
        {NULL, NULL}};
    luaL_newlib(L, keyboardLib);
    lua_setglobal(L, "keyboard");

    // Register window library
    luaL_Reg windowLib[] = {
        {"title", window_title},
        {"close", window_close},
        {"fullscreen", window_fullscreen},
        {"message", window_message},
        {NULL, NULL}};
    luaL_newlib(L, windowLib);
    lua_setglobal(L, "window");

    // Register util library
    luaL_Reg utilLib[] = {
        {"distance", util_distance},
        {"clamp", util_clamp},
        {"lerp", util_lerp},
        {"random", util_random},
        {"httpGet", http_get},
        {"intersect", util_intersect},
        {NULL, NULL}};
    luaL_newlib(L, utilLib);
    lua_setglobal(L, "util");

    // Load and execute the Lua script
    if (luaL_dofile(L, scriptPath))
    {
        LOG("Lua Error: %s\n", lua_tostring(L, -1));
        lua_close(L);
        L = NULL;
    }
}

void SetupBuffers(int width, int height)
{
    bufferWidth = width;
    bufferHeight = height;

    // Allocate double pixel buffers
    pixelsFront = (Uint32 *)malloc(bufferWidth * bufferHeight * sizeof(Uint32));
    pixelsBack = (Uint32 *)malloc(bufferWidth * bufferHeight * sizeof(Uint32));
    if (!pixelsFront || !pixelsBack)
    {
        LOG("Failed to allocate pixel buffers.\n");
        exit(1);
    }
    memset(pixelsFront, 0, bufferWidth * bufferHeight * sizeof(Uint32));
    memset(pixelsBack, 0, bufferWidth * bufferHeight * sizeof(Uint32));

    // Create texture with matching pixel format
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, bufferWidth, bufferHeight);
    if (!texture)
    {
        LOG("Failed to create texture: %s\n", SDL_GetError());
        free(pixelsFront);
        free(pixelsBack);
        exit(1);
    }
}

// Update pixels by calling Lua's update function with deltaTime
void UpdatePixelsFromLua(double deltaTime)
{
    lua_getglobal(L, "update");
    if (lua_isfunction(L, -1))
    {
        lua_pushnumber(L, deltaTime);

        if (lua_pcall(L, 1, 0, 0) != LUA_OK)
        {
            LOG("Lua Error in 'update': %s\n", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
    else
    {
        lua_pop(L, 1);
        LOG("Lua 'update' function not found.\n"); // Debug print
    }
}

void DrawBuffer()
{
    // Update the texture with the front buffer
    SDL_UpdateTexture(texture, NULL, pixelsFront, bufferWidth * sizeof(Uint32));

    // Clear the renderer
    SDL_RenderClear(renderer);

    // Calculate scaling
    int windowWidth, windowHeight;
    SDL_GetWindowSize(window, &windowWidth, &windowHeight);

    float bufferAspect = (float)bufferWidth / bufferHeight;
    float windowAspect = (float)windowWidth / windowHeight;

    SDL_Rect destRect;
    if (windowAspect > bufferAspect)
    {
        destRect.h = windowHeight;
        destRect.w = (int)(windowHeight * bufferAspect);
        destRect.x = (windowWidth - destRect.w) / 2;
        destRect.y = 0;
    }
    else
    {
        destRect.w = windowWidth;
        destRect.h = (int)(windowWidth / bufferAspect);
        destRect.x = 0;
        destRect.y = (windowHeight - destRect.h) / 2;
    }

    // Render the texture
    SDL_RenderCopy(renderer, texture, NULL, &destRect);

    // Present the renderer
    SDL_RenderPresent(renderer);
}

// Implement Lua functions here

int color_rgb(lua_State *L)
{
    int r = luaL_checkinteger(L, 1);
    int g = luaL_checkinteger(L, 2);
    int b = luaL_checkinteger(L, 3);

    if (r < 0 || r > 7 || g < 0 || g > 7 || b < 0 || b > 7)
    {
        return luaL_error(L, "RGB values must be between 0 and 7");
    }

    int encodedValue = EncodeColor(r, g, b);
    lua_pushinteger(L, encodedValue);
    return 1;
}

int color_hsv(lua_State *L)
{
    int h = luaL_checkinteger(L, 1);
    int s = luaL_checkinteger(L, 2);
    int v = luaL_checkinteger(L, 3);
    if (h < 0 || h > 7 || s < 0 || s > 7 || v < 0 || v > 7)
    {
        return luaL_error(L, "HSV values must be between 0 and 7");
    }

    float hue = h / 7.0f * 360.0f;
    float saturation = s / 7.0f;
    float value = v / 7.0f;

    float c = value * saturation;
    float x = c * (1 - fabsf(fmodf(hue / 60.0f, 2) - 1));
    float m = value - c;

    float r_prime, g_prime, b_prime;

    if (hue < 60)
    {
        r_prime = c;
        g_prime = x;
        b_prime = 0;
    }
    else if (hue < 120)
    {
        r_prime = x;
        g_prime = c;
        b_prime = 0;
    }
    else if (hue < 180)
    {
        r_prime = 0;
        g_prime = c;
        b_prime = x;
    }
    else if (hue < 240)
    {
        r_prime = 0;
        g_prime = x;
        b_prime = c;
    }
    else if (hue < 300)
    {
        r_prime = x;
        g_prime = 0;
        b_prime = c;
    }
    else
    {
        r_prime = c;
        g_prime = 0;
        b_prime = x;
    }

    int r = (int)((r_prime + m) * 7 + 0.5f); // Added 0.5f for rounding
    int g = (int)((g_prime + m) * 7 + 0.5f);
    int b = (int)((b_prime + m) * 7 + 0.5f);

    // Clamp values to [0,7]
    if (r > 7)
        r = 7;
    if (g > 7)
        g = 7;
    if (b > 7)
        b = 7;

    int encodedValue = EncodeColor(r, g, b);
    lua_pushinteger(L, encodedValue);
    return 1;
}

int color_greyscale(lua_State *L)
{
    Uint32 encodedColor = luaL_checkinteger(L, 1);
    // Decode the color to get the mapped Uint32 color
    Uint32 mappedColor = DecodeColor(encodedColor);

    // Extract r, g, b components using SDL_GetRGBA
    Uint8 r, g, b, a;
    SDL_GetRGBA(mappedColor, globalFormat, &r, &g, &b, &a);

    // Calculate the grayscale value
    Uint8 gray = (Uint8)(((int)r + (int)g + (int)b) / 3);
    int grayIndex = gray * 7 / 255; // Map 0-255 to 0-7
    if (grayIndex > 7)
        grayIndex = 7;

    // Encode the grayscale color
    int greyscaleColor = EncodeColor(grayIndex, grayIndex, grayIndex);
    lua_pushinteger(L, greyscaleColor);
    return 1;
}

int texture_fromShader(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TFUNCTION);
    int width = luaL_checkinteger(L, 2);
    int height = luaL_checkinteger(L, 3);

    lua_newtable(L);
    for (int y = 0; y < height; y++)
    {
        lua_newtable(L);
        for (int x = 0; x < width; x++)
        {
            lua_pushvalue(L, 1); // Push the shader function
            lua_pushinteger(L, x);
            lua_pushinteger(L, y);
            if (lua_pcall(L, 2, 1, 0) != LUA_OK)
            {
                LOG("Error in Shader: %s\n", lua_tostring(L, -1));
                lua_pop(L, 1);
                lua_pushinteger(L, EncodeColor(0, 0, 0)); // default color if error
            }
            int value = lua_tointeger(L, -1);
            lua_pop(L, 1);
            lua_pushinteger(L, value);
            lua_rawseti(L, -2, x + 1);
        }
        lua_rawseti(L, -2, y + 1);
    }
    return 1;
}

int texture_fromRom(lua_State *L)
{
    const char *imageName = luaL_checkstring(L, 1);

    if (strlen(romPathGlobal) == 0)
    {
        return luaL_error(L, "ROM path not provided.");
    }

    FILE *file = fopen(romPathGlobal, "rb");
    if (!file)
    {
        return luaL_error(L, "Failed to open ROM file: %s", romPathGlobal);
    }

    char header[4];
    fread(header, 1, 4, file);
    if (strncmp(header, "imag", 4) != 0)
    {
        fclose(file);
        return luaL_error(L, "Invalid ROM file header");
    }

    unsigned char numImages;
    fread(&numImages, 1, 1, file);

    unsigned int width, height;
    Uint16 *tempPixels = NULL;
    bool found = false;
    for (int i = 0; i < numImages; ++i)
    {
        unsigned int numPixelsInImage;
        char imgName[5] = {0};
        fread(&numPixelsInImage, 4, 1, file);
        fread(imgName, 4, 1, file);
        fread(&width, 4, 1, file);
        fread(&height, 4, 1, file);

        if (strncmp(imgName, imageName, 4) == 0)
        {
            if (numPixelsInImage != width * height)
            {
                fclose(file);
                return luaL_error(L, "Image size does not match expected dimensions");
            }

            if (numPixelsInImage > 1000000)
            {
                fclose(file);
                return luaL_error(L, "Image too large to load");
            }

            tempPixels = (Uint16 *)malloc(numPixelsInImage * sizeof(Uint16));
            if (!tempPixels)
            {
                fclose(file);
                return luaL_error(L, "Failed to allocate memory for image data");
            }
            fread(tempPixels, sizeof(Uint16), numPixelsInImage, file);
            found = true;
            break;
        }
        else
        {
            fseek(file, numPixelsInImage * sizeof(Uint16), SEEK_CUR);
        }
    }

    fclose(file);

    if (!found)
    {
        return luaL_error(L, "Image '%s' not found in ROM file", imageName);
    }

    lua_newtable(L);
    for (unsigned int y = 0; y < height; ++y)
    {
        lua_newtable(L);
        for (unsigned int x = 0; x < width; ++x)
        {
            unsigned int index = y * width + x;
            int color = tempPixels[index];

            lua_pushinteger(L, color);
            lua_rawseti(L, -2, x + 1);
        }
        lua_rawseti(L, -2, y + 1);
    }

    free(tempPixels);
    return 1;
}

int drawing_shader(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TFUNCTION);

    for (int y = 0; y < bufferHeight; y++)
    {
        for (int x = 0; x < bufferWidth; x++)
        {
            lua_pushvalue(L, 1);   // Push the shader function
            lua_pushinteger(L, x); // Push x
            lua_pushinteger(L, y); // Push y
            if (lua_pcall(L, 2, 1, 0) != LUA_OK)
            {
                LOG("Error in Shader: %s\n", lua_tostring(L, -1));
                lua_pop(L, 1);
                // Set default color as black with full opacity
                pixelsBack[y * bufferWidth + x] = (0 << 24) | (0 << 16) | (0 << 8) | 255;
                continue;
            }
            int value = lua_tointeger(L, -1);
            lua_pop(L, 1);

            if (value < 1 || value > 512)
            {
                // Set default color as black with full opacity
                pixelsBack[y * bufferWidth + x] = (0 << 24) | (0 << 16) | (0 << 8) | 255;
                continue;
            }

            // Decode the color
            Uint32 mappedColor = DecodeColor(value);
            Uint8 r, g, b, a;
            SDL_GetRGBA(mappedColor, globalFormat, &r, &g, &b, &a);

            // Write the decoded color to the back buffer
            pixelsBack[y * bufferWidth + x] = (r << 24) | (g << 16) | (b << 8) | a; // RGBA
        }
    }

    return 0;
}

int drawing_rect(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    int xOffset = luaL_checkinteger(L, 2);
    int yOffset = luaL_checkinteger(L, 3);

    int textureHeight = lua_objlen(L, 1); // Updated to lua_objlen

    for (int y = 1; y <= textureHeight; y++)
    {
        lua_rawgeti(L, 1, y);
        int textureWidth = lua_objlen(L, -1); // Updated to lua_objlen

        for (int x = 1; x <= textureWidth; x++)
        {
            lua_rawgeti(L, -1, x);
            int value = lua_tointeger(L, -1);
            lua_pop(L, 1);

            int destX = xOffset + x - 1;
            int destY = yOffset + y - 1;

            if (destX >= 0 && destX < bufferWidth && destY >= 0 && destY < bufferHeight)
            {
                if (value <= 0 || value > 512)
                    continue;

                // Decode the color
                Uint32 mappedColor = DecodeColor(value);
                Uint8 r, g, b, a;
                SDL_GetRGBA(mappedColor, globalFormat, &r, &g, &b, &a);

                // Write to the back buffer
                pixelsBack[destY * bufferWidth + destX] = (r << 24) | (g << 16) | (b << 8) | a; // RGBA
            }
        }
        lua_pop(L, 1);
    }
    return 0;
}

int drawing_circle(lua_State *L)
{
    int centerX = luaL_checkinteger(L, 1);
    int centerY = luaL_checkinteger(L, 2);
    int radius = luaL_checkinteger(L, 3);
    int color = luaL_checkinteger(L, 4);

    // Decode the color
    Uint32 mappedColor = DecodeColor(color);
    Uint8 r, g, b, a;
    SDL_GetRGBA(mappedColor, globalFormat, &r, &g, &b, &a);

    for (int y = -radius; y <= radius; y++)
    {
        for (int x = -radius; x <= radius; x++)
        {
            if (x * x + y * y <= radius * radius)
            {
                int destX = centerX + x;
                int destY = centerY + y;
                if (destX >= 0 && destX < bufferWidth && destY >= 0 && destY < bufferHeight)
                {
                    // Write to the back buffer
                    pixelsBack[destY * bufferWidth + destX] = (r << 24) | (g << 16) | (b << 8) | a; // RGBA
                }
            }
        }
    }

    return 0;
}

int drawing_line(lua_State *L)
{
    int x1 = luaL_checkinteger(L, 1);
    int y1 = luaL_checkinteger(L, 2);
    int x2 = luaL_checkinteger(L, 3);
    int y2 = luaL_checkinteger(L, 4);
    int color = luaL_checkinteger(L, 5);

    // Decode the color
    Uint32 mappedColor = DecodeColor(color);
    Uint8 r, g, b, a;
    SDL_GetRGBA(mappedColor, globalFormat, &r, &g, &b, &a);

    int dx = abs(x2 - x1);
    int dy = abs(y2 - y1);
    int sx = x1 < x2 ? 1 : -1;
    int sy = y1 < y2 ? 1 : -1;
    int err = dx - dy;

    while (true)
    {
        if (x1 >= 0 && x1 < bufferWidth && y1 >= 0 && y1 < bufferHeight)
        {
            // Write to the back buffer
            pixelsBack[y1 * bufferWidth + x1] = (r << 24) | (g << 16) | (b << 8) | a; // RGBA
        }
        if (x1 == x2 && y1 == y2)
            break;
        int e2 = 2 * err;
        if (e2 > -dy)
        {
            err -= dy;
            x1 += sx;
        }
        if (e2 < dx)
        {
            err += dx;
            y1 += sy;
        }
    }

    return 0;
}

int drawing_pixel(lua_State *L)
{
    int x = luaL_checkinteger(L, 1);
    int y = luaL_checkinteger(L, 2);
    int color = luaL_checkinteger(L, 3);

    if (x >= 0 && x < bufferWidth && y >= 0 && y < bufferHeight)
    {
        // Decode the color
        Uint32 mappedColor = DecodeColor(color);
        Uint8 r, g, b, a;
        SDL_GetRGBA(mappedColor, globalFormat, &r, &g, &b, &a);

        // Write to the back buffer
        pixelsBack[y * bufferWidth + x] = (r << 24) | (g << 16) | (b << 8) | a; // RGBA
    }
    return 0;
}

int mouse_center(lua_State *L)
{
    int windowWidth, windowHeight;
    SDL_GetWindowSize(window, &windowWidth, &windowHeight);
    SDL_WarpMouseInWindow(window, windowWidth / 2, windowHeight / 2);
    return 0;
}

int mouse_visible(lua_State *L)
{
    bool visible = lua_toboolean(L, 1);
    SDL_ShowCursor(visible ? SDL_ENABLE : SDL_DISABLE);
    return 0;
}

int window_message(lua_State *L)
{
    const char *text = luaL_checkstring(L, 1);
    const char *title = SDL_GetWindowTitle(window);

    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, title, text, window);
    return 0;
}

int http_get(lua_State *L)
{
    // Implement a cross-platform HTTP GET request if necessary
    lua_pushnil(L);
    lua_pushinteger(L, 501); // Not Implemented
    return 2;
}

int util_distance(lua_State *L)
{
    double x1 = luaL_checknumber(L, 1);
    double y1 = luaL_checknumber(L, 2);
    double x2 = luaL_checknumber(L, 3);
    double y2 = luaL_checknumber(L, 4);

    double dx = x2 - x1;
    double dy = y2 - y1;
    double distance = sqrt(dx * dx + dy * dy);

    lua_pushnumber(L, distance);
    return 1;
}

int util_random(lua_State *L)
{
    int argc = lua_gettop(L);
    if (argc == 0)
    {
        lua_pushnumber(L, (double)rand() / RAND_MAX);
    }
    else if (argc == 2)
    {
        double min = luaL_checknumber(L, 1);
        double max = luaL_checknumber(L, 2);
        double random = min + ((double)rand() / RAND_MAX) * (max - min);
        lua_pushnumber(L, random);
    }
    else
    {
        return luaL_error(L, "Invalid arguments to util.random");
    }
    return 1;
}

int util_clamp(lua_State *L)
{
    double value = luaL_checknumber(L, 1);
    double minVal = luaL_checknumber(L, 2);
    double maxVal = luaL_checknumber(L, 3);

    if (value < minVal)
        value = minVal;
    else if (value > maxVal)
        value = maxVal;

    lua_pushnumber(L, value);
    return 1;
}

int util_lerp(lua_State *L)
{
    double start = luaL_checknumber(L, 1);
    double end = luaL_checknumber(L, 2);
    double t = luaL_checknumber(L, 3);

    double result = start + t * (end - start);
    lua_pushnumber(L, result);
    return 1;
}

int util_intersect(lua_State *L)
{
    double x1 = luaL_checknumber(L, 1);
    double y1 = luaL_checknumber(L, 2);
    double width1 = luaL_checknumber(L, 3);
    double height1 = luaL_checknumber(L, 4);

    double x2 = luaL_checknumber(L, 5);
    double y2 = luaL_checknumber(L, 6);
    double width2 = luaL_checknumber(L, 7);
    double height2 = luaL_checknumber(L, 8);

    double halfWidth1 = width1 / 2.0;
    double halfHeight1 = height1 / 2.0;
    double halfWidth2 = width2 / 2.0;
    double halfHeight2 = height2 / 2.0;

    double centerX1 = x1 + halfWidth1;
    double centerY1 = y1 + halfHeight1;
    double centerX2 = x2 + halfWidth2;
    double centerY2 = y2 + halfHeight2;

    double deltaX = centerX2 - centerX1;
    double deltaY = centerY2 - centerY1;

    double combinedHalfWidth = halfWidth1 + halfWidth2;
    double combinedHalfHeight = halfHeight1 + halfHeight2;

    if (fabs(deltaX) < combinedHalfWidth && fabs(deltaY) < combinedHalfHeight)
    {
        double overlapX = combinedHalfWidth - fabs(deltaX);
        double overlapY = combinedHalfHeight - fabs(deltaY);

        if (overlapX < overlapY)
        {
            if (deltaX > 0)
            {
                lua_pushnumber(L, overlapX);
                lua_pushnumber(L, 0);
                lua_pushnumber(L, -overlapX);
                lua_pushnumber(L, 0);
            }
            else
            {
                lua_pushnumber(L, -overlapX);
                lua_pushnumber(L, 0);
                lua_pushnumber(L, overlapX);
                lua_pushnumber(L, 0);
            }
        }
        else
        {
            if (deltaY > 0)
            {
                lua_pushnumber(L, 0);
                lua_pushnumber(L, overlapY);
                lua_pushnumber(L, 0);
                lua_pushnumber(L, -overlapY);
            }
            else
            {
                lua_pushnumber(L, 0);
                lua_pushnumber(L, -overlapY);
                lua_pushnumber(L, 0);
                lua_pushnumber(L, overlapY);
            }
        }
        return 4;
    }
    else
    {
        lua_pushnumber(L, 0);
        lua_pushnumber(L, 0);
        lua_pushnumber(L, 0);
        lua_pushnumber(L, 0);
        return 4;
    }
}

int keyboard_down(lua_State *L)
{
    const char *key = luaL_checkstring(L, 1);
    SDL_Scancode scancode;

    if (strlen(key) == 1)
    {
        int keycode = tolower(key[0]);
        scancode = SDL_GetScancodeFromKey(keycode);
    }
    else
    {
        if (strcmp(key, "enter") == 0)
            scancode = SDL_SCANCODE_RETURN;
        else if (strcmp(key, "shift") == 0)
            scancode = SDL_SCANCODE_LSHIFT; // or SDL_SCANCODE_RSHIFT
        else if (strcmp(key, "control") == 0)
            scancode = SDL_SCANCODE_LCTRL; // or SDL_SCANCODE_RCTRL
        else if (strcmp(key, "alt") == 0)
            scancode = SDL_SCANCODE_LALT; // or SDL_SCANCODE_RALT
        else if (strcmp(key, "escape") == 0)
            scancode = SDL_SCANCODE_ESCAPE;
        else if (strcmp(key, "back") == 0)
            scancode = SDL_SCANCODE_BACKSPACE;
        else if (strcmp(key, "tab") == 0)
            scancode = SDL_SCANCODE_TAB;
        else if (strcmp(key, "left") == 0)
            scancode = SDL_SCANCODE_LEFT;
        else if (strcmp(key, "right") == 0)
            scancode = SDL_SCANCODE_RIGHT;
        else if (strcmp(key, "up") == 0)
            scancode = SDL_SCANCODE_UP;
        else if (strcmp(key, "down") == 0)
            scancode = SDL_SCANCODE_DOWN;
        else
            return luaL_error(L, "Unrecognized key: %s", key);
    }

    const Uint8 *state = SDL_GetKeyboardState(NULL);
    lua_pushboolean(L, state[scancode]);
    return 1;
}

int mouse_position(lua_State *L)
{
    int x, y;
    SDL_GetMouseState(&x, &y);

    int windowWidth, windowHeight;
    SDL_GetWindowSize(window, &windowWidth, &windowHeight);

    float bufferAspectRatio = (float)bufferWidth / bufferHeight;
    float windowAspectRatio = (float)windowWidth / windowHeight;

    int destW, destH, offsetX = 0, offsetY = 0;

    if (windowAspectRatio > bufferAspectRatio)
    {
        destH = windowHeight;
        destW = (int)(windowHeight * bufferAspectRatio);
        offsetX = (windowWidth - destW) / 2;
    }
    else
    {
        destW = windowWidth;
        destH = (int)(windowWidth / bufferAspectRatio);
        offsetY = (windowHeight - destH) / 2;
    }

    float scaleX = (float)bufferWidth / destW;
    float scaleY = (float)bufferHeight / destH;

    int bufferX = (int)((x - offsetX) * scaleX);
    int bufferY = (int)((y - offsetY) * scaleY);

    if (bufferX < 0 || bufferX >= bufferWidth || bufferY < 0 || bufferY >= bufferHeight)
    {
        lua_pushnil(L);
        lua_pushnil(L);
    }
    else
    {
        lua_pushinteger(L, bufferX);
        lua_pushinteger(L, bufferY);
    }
    return 2;
}

int mouse_down(lua_State *L)
{
    int button = luaL_checkinteger(L, 1);
    Uint32 sdlButton;
    switch (button)
    {
    case 1:
        sdlButton = SDL_BUTTON_LEFT;
        break;
    case 2:
        sdlButton = SDL_BUTTON_RIGHT;
        break;
    case 3:
        sdlButton = SDL_BUTTON_MIDDLE;
        break;
    default:
        return luaL_error(L, "Invalid button");
    }
    Uint32 state = SDL_GetMouseState(NULL, NULL);
    lua_pushboolean(L, state & SDL_BUTTON(sdlButton));
    return 1;
}

int window_title(lua_State *L)
{
    const char *title = luaL_checkstring(L, 1);
    SDL_SetWindowTitle(window, title);
    return 0;
}

int window_fullscreen(lua_State *L)
{
    bool fullscreen = lua_toboolean(L, 1);
    if (fullscreen && (!isFullscreen))
    {
        if (SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP) != 0)
        {
            LOG("Failed to set fullscreen: %s\n", SDL_GetError());
        }
        else
        {
            isFullscreen = true;
        }
    }
    else if ((!fullscreen) && isFullscreen)
    {
        if (SDL_SetWindowFullscreen(window, 0) != 0)
        {
            LOG("Failed to exit fullscreen: %s\n", SDL_GetError());
        }
        else
        {
            isFullscreen = false;
        }
    }
    return 0;
}

int window_close(lua_State *L)
{
    running = false;
    return 0;
}

int main(int argc, char *argv[])
{
    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0)
    {
        LOG("SDL_Init Error: %s\n", SDL_GetError());
        return 1;
    }

    const char *scriptPath;
    const char *romPath;

    if (argc < 2)
    {
        // No arguments provided; use default paths
        scriptPath = "main.lua";
        romPath = "rom.rom";
    }
    else
    {
        // Use provided script path
        scriptPath = argv[1];
        // Use provided ROM path if available; otherwise, default to "rom.rom"
        romPath = (argc >= 3) ? argv[2] : "rom.rom";
    }

    romPathGlobal = romPath;
    // Create a temporary window and renderer to initialize the globalFormat
    // This is necessary because SDL needs a renderer to get a pixel format
    window = SDL_CreateWindow("Temp", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1, 1, SDL_WINDOW_HIDDEN);
    if (!window)
    {
        LOG("SDL_CreateWindow Error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer)
    {
        LOG("SDL_CreateRenderer Error: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    globalFormat = SDL_AllocFormat(SDL_PIXELFORMAT_RGBA8888);
    if (!globalFormat)
    {
        LOG("SDL_AllocFormat Error: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Destroy the temporary window and renderer
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    window = NULL;
    renderer = NULL;

    // Initialize Lua
    InitializeLua(scriptPath);
    if (!L)
    {
        SDL_FreeFormat(globalFormat);
        SDL_Quit();
        return 1;
    }

    // Get bufferWidth and bufferHeight from Lua
    lua_getglobal(L, "width");
    if (!lua_isinteger_custom(L, -1))
    {
        LOG("Expected integer for 'width'\n");
        lua_close(L);
        SDL_FreeFormat(globalFormat);
        SDL_Quit();
        return 1;
    }
    bufferWidth = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);

    lua_getglobal(L, "height");
    if (!lua_isinteger_custom(L, -1))
    {
        LOG("Expected integer for 'height'\n");
        lua_close(L);
        SDL_FreeFormat(globalFormat);
        SDL_Quit();
        return 1;
    }
    bufferHeight = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);

    // Check 'noConsole' global variable
    lua_getglobal(L, "noConsole");
    if (lua_isboolean(L, -1) && lua_toboolean(L, -1))
    {
// Hide/Delete the console
#ifdef _WIN32
        // On Windows, detach the console
        FreeConsole();
#else
        // On Unix-like systems, redirect stdout and stderr to /dev/null
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);
#endif

        // Set 'suppress' to true
        suppress = true;
    }
    lua_pop(L, 1);

    // If 'noConsole' is not true, handle 'suppress' as usual
    if (!suppress)
    {
        lua_getglobal(L, "suppress");
        if (lua_isboolean(L, -1))
        {
            suppress = lua_toboolean(L, -1);
        }
        lua_pop(L, 1);
    }

    // Get window title from Lua
    const char *windowTitle = "PLF Window";
    lua_getglobal(L, "title");
    if (lua_isstring(L, -1))
    {
        windowTitle = lua_tostring(L, -1);
    }
    lua_pop(L, 1);

    // Calculate buffer aspect ratio
    float bufferAspect = (float)bufferWidth / (float)bufferHeight;

    // Get the current display index (assuming display 0)
    int displayIndex = 0;

    // Get the current display mode
    SDL_DisplayMode displayMode;
    if (SDL_GetCurrentDisplayMode(displayIndex, &displayMode) != 0)
    {
        printf("SDL_GetCurrentDisplayMode Error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    int screenWidth = displayMode.w;
    int screenHeight = displayMode.h;

    // Determine the minimum screen dimension
    int minScreenDim = (screenWidth < screenHeight) ? screenWidth : screenHeight;

    // Calculate maximum window size (half of the minimum screen dimension)
    int maxWindowSize = minScreenDim / 2;

    // Calculate window size while maintaining aspect ratio
    int windowWidth = maxWindowSize;
    int windowHeight = (int)(maxWindowSize / bufferAspect);

    // If height exceeds maxWindowSize, adjust width instead
    if (windowHeight > maxWindowSize)
    {
        windowHeight = maxWindowSize;
        windowWidth = (int)(maxWindowSize * bufferAspect);
    }

    // Create SDL_Window and SDL_Renderer with the actual buffer size
    window = SDL_CreateWindow(windowTitle, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, windowWidth, windowHeight, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window)
    {
        LOG("SDL_CreateWindow Error: %s\n", SDL_GetError());
        lua_close(L);
        SDL_FreeFormat(globalFormat);
        SDL_Quit();
        return 1;
    }

    // Create renderer
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer)
    {
        LOG("SDL_CreateRenderer Error: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        lua_close(L);
        SDL_FreeFormat(globalFormat);
        SDL_Quit();
        return 1;
    }

    // Setup double buffers
    SetupBuffers(bufferWidth, bufferHeight);

    // Seed random number generator
    srand((unsigned int)time(NULL));

    // Initialize timing for deltaTime
    Uint64 now = SDL_GetPerformanceCounter();
    Uint64 lastTime = 0;
    double deltaTime = 0.0;

    // Desired frame time in seconds (1.0 / fps)
    double desiredFrameTime = 0.0;

    // Main loop
    SDL_Event e;

    while (running)
    {
        // Frame start time
        Uint64 frameStart = SDL_GetPerformanceCounter();

        // Calculate deltaTime
        lastTime = now;
        now = SDL_GetPerformanceCounter();
        deltaTime = (double)(now - lastTime) / (double)SDL_GetPerformanceFrequency();

        // Handle events
        while (SDL_PollEvent(&e))
        {
            // Handle events
            if (e.type == SDL_QUIT)
            {
                running = false;
            }
            else if (e.type == SDL_KEYDOWN)
            {
                if (e.key.keysym.sym == SDLK_F11)
                {
                    if (!isFullscreen)
                    {
                        if (SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP) != 0)
                        {
                            LOG("Failed to set fullscreen: %s\n", SDL_GetError());
                        }
                        else
                        {
                            isFullscreen = true;
                        }
                    }
                    else
                    {
                        if (SDL_SetWindowFullscreen(window, 0) != 0)
                        {
                            LOG("Failed to exit fullscreen: %s\n", SDL_GetError());
                        }
                        else
                        {
                            isFullscreen = false;
                        }
                    }
                }
            }
            else if (e.type == SDL_MOUSEBUTTONDOWN)
            {
                int luaButton = 0; // Initialize luaButton to 0 (invalid)

                // Map SDL button codes to desired Lua button numbers
                switch (e.button.button)
                {
                case SDL_BUTTON_LEFT:
                    luaButton = 1; // Left button
                    break;
                case SDL_BUTTON_RIGHT:
                    luaButton = 2; // Right button
                    break;
                case SDL_BUTTON_MIDDLE:
                    luaButton = 3; // Middle button
                    break;
                default:
                    // If it's not left, right, or middle button, do nothing
                    break;
                }

                // Proceed only if luaButton is valid (1, 2, or 3)
                if (luaButton != 0)
                {
                    lua_getglobal(L, "mouseDown"); // Get the 'mouseDown' function from Lua

                    if (lua_isfunction(L, -1))
                    {
                        lua_pushinteger(L, luaButton); // Push the mapped button number to Lua

                        // Call the Lua function with 1 argument and 0 return values
                        if (lua_pcall(L, 1, 0, 0) != LUA_OK)
                        {
                            LOG("Error in Mouse Down: %s\n", lua_tostring(L, -1));
                            lua_pop(L, 1); // Remove error message from the stack
                        }
                    }
                    else
                    {
                        lua_pop(L, 1); // Remove non-function value from the stack
                    }
                }
            }
            else if (e.type == SDL_MOUSEBUTTONUP)
            {
                int luaButton = 0; // Initialize luaButton to 0 (invalid)

                // Map SDL button codes to desired Lua button numbers
                switch (e.button.button)
                {
                case SDL_BUTTON_LEFT:
                    luaButton = 1; // Left button
                    break;
                case SDL_BUTTON_RIGHT:
                    luaButton = 2; // Right button
                    break;
                case SDL_BUTTON_MIDDLE:
                    luaButton = 3; // Middle button
                    break;
                default:
                    // If it's not left, right, or middle button, do nothing
                    break;
                }

                // Proceed only if luaButton is valid (1, 2, or 3)
                if (luaButton != 0)
                {
                    lua_getglobal(L, "mouseUp"); // Get the 'mouseUp' function from Lua

                    if (lua_isfunction(L, -1))
                    {
                        lua_pushinteger(L, luaButton); // Push the mapped button number to Lua

                        // Call the Lua function with 1 argument and 0 return values
                        if (lua_pcall(L, 1, 0, 0) != LUA_OK)
                        {
                            LOG("Error in Mouse Up: %s\n", lua_tostring(L, -1));
                            lua_pop(L, 1); // Remove error message from the stack
                        }
                    }
                    else
                    {
                        lua_pop(L, 1); // Remove non-function value from the stack
                    }
                }
            }
        }

        // Update pixels by calling Lua's update function with deltaTime
        UpdatePixelsFromLua(deltaTime);

        // Swap front and back buffers
        Uint32 *temp = pixelsFront;
        pixelsFront = pixelsBack;
        pixelsBack = temp;

        // Clear the back buffer
        memset(pixelsBack, 0, bufferWidth * bufferHeight * sizeof(Uint32));

        // Render the front buffer
        DrawBuffer();

        // Frame end time
        Uint64 frameEnd = SDL_GetPerformanceCounter();
        double frameDuration = (double)(frameEnd - frameStart) / (double)SDL_GetPerformanceFrequency();

        // Retrieve 'fps' from Lua
        lua_getglobal(L, "fps");
        if (lua_isnumber(L, -1))
        {
            double fps = lua_tonumber(L, -1);
            if (fps > 0.0)
            {
                desiredFrameTime = 1.0 / fps;

                if (frameDuration < desiredFrameTime)
                {
                    double delayTimeSec = desiredFrameTime - frameDuration;
                    Uint32 delayTimeMs = (Uint32)(delayTimeSec * 1000.0);
                    if (delayTimeMs > 0)
                    {
                        SDL_Delay(delayTimeMs);
                    }
                }
            }
            else
            {
                // If fps is zero or negative, run as fast as possible
                // No delay
            }
        }
        else
        {
            // 'fps' not set or not a number, run as fast as possible
            // No delay
        }
        lua_pop(L, 1); // Remove 'fps' from the stack
    }

    // Clean up
    free(pixelsFront);
    free(pixelsBack);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    lua_close(L);
    SDL_FreeFormat(globalFormat);
    SDL_Quit();

    return 0;
}
