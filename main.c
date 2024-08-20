#include <windows.h>
#include <stdio.h>
#include <stdbool.h>
#include <process.h>
#include <winhttp.h>
#include <ctype.h>
#include <string.h>
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

// Global variables
HDC hdcMem = NULL;
HBITMAP hBitmap = NULL;
BYTE *bitmapData = NULL;
lua_State *L = NULL;
HWND hwndGlobal = NULL;
int bufferHeight = 0, bufferWidth = 0, updateInterval = 0;
HANDLE updateThread = NULL;
bool running = true;
bool isFullscreen = false;
RECT windowRect = {0};
const char *romPathGlobal = NULL;

void InitializeLua(const char *scriptPath);
void SetupBuffer(HDC hdc, int width, int height);
void UpdatePixelsFromLua(double dt);
unsigned __stdcall LuaUpdateThread(void *param);
void DrawBuffer(HWND hwnd);
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
int color_rgb(lua_State *L);
int color_hsv(lua_State *L);
int color_greyscale(lua_State *L);
int texture_fromShader(lua_State *L);
int texture_fromRom(lua_State *L);
int drawing_shader(lua_State *L);
int drawing_rect(lua_State *L);
int mouse_position(lua_State *L);
int mouse_down(lua_State *L);
int keyboard_down(lua_State *L);
int window_title(lua_State *L);
int window_close(lua_State *L);
int mouse_visible(lua_State *L);
int window_fullscreen(lua_State *L);
int window_message(lua_State *L);
int http_get(lua_State *L);
int util_distance(lua_State *L);
int util_random(lua_State *L);
int util_clamp(lua_State *L);
int util_lerp(lua_State *L);
int util_intersect(lua_State *L);

void InitializeLua(const char *scriptPath)
{
    L = luaL_newstate();
    luaL_openlibs(L);

    luaL_Reg colorLib[] = {
        {"rgb", color_rgb},
        {"hsv", color_hsv},
        {"greyscale", color_greyscale},
        {NULL, NULL}};

    luaL_newlib(L, colorLib);
    lua_setglobal(L, "color");

    luaL_Reg drawingLib[] = {
        {"shader", drawing_shader},
        {"rect", drawing_rect},
        {NULL, NULL}};

    luaL_newlib(L, drawingLib);
    lua_setglobal(L, "drawing");

    luaL_Reg textureLib[] = {
        {"fromShader", texture_fromShader},
        {"fromRom", texture_fromRom},
        {NULL, NULL}};

    luaL_newlib(L, textureLib);
    lua_setglobal(L, "texture");

    luaL_Reg mouseLib[] = {
        {"position", mouse_position},
        {"down", mouse_down},
        {"visible", mouse_visible},
        {NULL, NULL}};

    luaL_newlib(L, mouseLib);
    lua_setglobal(L, "mouse");

    luaL_Reg keyboardLib[] = {
        {"down", keyboard_down},
        {NULL, NULL}};

    luaL_newlib(L, keyboardLib);
    lua_setglobal(L, "keyboard");

    luaL_Reg windowLib[] = {
        {"title", window_title},
        {"close", window_close},
        {"fullscreen", window_fullscreen},
        {"message", window_message},
        {NULL, NULL}};

    luaL_newlib(L, windowLib);
    lua_setglobal(L, "window");

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

    if (luaL_dofile(L, scriptPath))
    {
        MessageBox(NULL, lua_tostring(L, -1), "Lua Error :\\", MB_OK);
        lua_close(L);
        L = NULL;
    }
}

void SetupBuffer(HDC hdc, int width, int height)
{
    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // Top-down DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 24;
    bmi.bmiHeader.biCompression = BI_RGB;

    hBitmap = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, (void **)&bitmapData, NULL, 0);
    hdcMem = CreateCompatibleDC(hdc);
    SelectObject(hdcMem, hBitmap);
}

void UpdatePixelsFromLua(double dt)
{
    lua_getglobal(L, "update");
    if (lua_isfunction(L, -1))
    {
        lua_pushnumber(L, dt);

        if (lua_pcall(L, 1, 0, 0) != LUA_OK)
        {
            MessageBox(NULL, lua_tostring(L, -1), "Error in Update :\\", MB_OK);
            lua_pop(L, 1);
        }
    }
    else
    {
        lua_pop(L, 1);
    }
}

unsigned __stdcall LuaUpdateThread(void *param)
{
    DWORD lastTime = GetTickCount(); // Initialize lastTime

    while (running)
    {
        DWORD startTime = GetTickCount();
        double dt = (double)(startTime - lastTime) / 1000.0; // Calculate delta time in seconds

        UpdatePixelsFromLua(dt); // Pass dt to the Lua update function

        DWORD elapsedTime = GetTickCount() - startTime;
        DWORD sleepTime = ((DWORD)updateInterval > elapsedTime) ? (updateInterval - elapsedTime) : 0;

        InvalidateRect(hwndGlobal, NULL, FALSE); // Request a window repaint
        Sleep(sleepTime);

        lastTime = startTime; // Update lastTime for the next iteration
    }
    return 0;
}

void DrawBuffer(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rect;
    GetClientRect(hwnd, &rect);

    // Calculate the aspect ratio of the buffer and the window
    float bufferAspectRatio = (float)bufferWidth / bufferHeight;
    float windowAspectRatio = (float)(rect.right - rect.left) / (rect.bottom - rect.top);

    int drawWidth, drawHeight;
    int offsetX = 0, offsetY = 0;

    if (windowAspectRatio > bufferAspectRatio)
    {
        // Window is wider than buffer aspect ratio, so we add black bars on the sides
        drawHeight = rect.bottom - rect.top;
        drawWidth = (int)(drawHeight * bufferAspectRatio);
        offsetX = (rect.right - rect.left - drawWidth) / 2;
    }
    else
    {
        // Window is taller than buffer aspect ratio, so we add black bars on the top and bottom
        drawWidth = rect.right - rect.left;
        drawHeight = (int)(drawWidth / bufferAspectRatio);
        offsetY = (rect.bottom - rect.top - drawHeight) / 2;
    }

    // Fill the window with black color
    HBRUSH blackBrush = (HBRUSH)GetStockObject(BLACK_BRUSH);
    FillRect(hdc, &rect, blackBrush);

    // Draw the buffer centered with black bars on the sides or top/bottom
    SetStretchBltMode(hdc, STRETCH_ANDSCANS);
    StretchBlt(hdc, offsetX, offsetY, drawWidth, drawHeight, hdcMem, 0, 0, bufferWidth, bufferHeight, SRCCOPY);

    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_KEYDOWN:
        if (wParam == VK_F11)
        {
            if (!isFullscreen)
            {
                GetWindowRect(hwnd, &windowRect);

                HMONITOR hMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
                MONITORINFO monitorInfo = {sizeof(MONITORINFO)};
                GetMonitorInfo(hMonitor, &monitorInfo);

                SetWindowLong(hwnd, GWL_STYLE, WS_POPUP);
                SetWindowPos(hwnd, HWND_TOP, monitorInfo.rcMonitor.left, monitorInfo.rcMonitor.top,
                             monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
                             monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
                             SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);

                isFullscreen = true;
            }
            else
            {
                SetWindowLong(hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW);
                SetWindowPos(hwnd, HWND_TOP, windowRect.left, windowRect.top,
                             windowRect.right - windowRect.left, windowRect.bottom - windowRect.top,
                             SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);

                isFullscreen = false;
            }
            return 0;
        }
        break;

    case WM_LBUTTONDOWN:
        lua_getglobal(L, "down");
        if (lua_isfunction(L, -1))
        {
            lua_pushinteger(L, 1);
            if (lua_pcall(L, 1, 0, 0) != LUA_OK)
            {
                MessageBox(NULL, lua_tostring(L, -1), "Error in Mouse Down", MB_OK);
            }
        }
        lua_pop(L, 1);
        break;

    case WM_LBUTTONUP:
        lua_getglobal(L, "up");
        if (lua_isfunction(L, -1))
        {
            lua_pushinteger(L, 1);
            if (lua_pcall(L, 1, 0, 0) != LUA_OK)
            {
                MessageBox(NULL, lua_tostring(L, -1), "Error in Mouse Up", MB_OK);
            }
        }
        lua_pop(L, 1);
        break;

    case WM_RBUTTONDOWN:
        lua_getglobal(L, "down");
        if (lua_isfunction(L, -1))
        {
            lua_pushinteger(L, 2);
            if (lua_pcall(L, 1, 0, 0) != LUA_OK)
            {
                MessageBox(NULL, lua_tostring(L, -1), "Error in Mouse Down", MB_OK);
            }
        }
        lua_pop(L, 1);
        break;

    case WM_RBUTTONUP:
        lua_getglobal(L, "up");
        if (lua_isfunction(L, -1))
        {
            lua_pushinteger(L, 2);
            if (lua_pcall(L, 1, 0, 0) != LUA_OK)
            {
                MessageBox(NULL, lua_tostring(L, -1), "Error in Mouse Up", MB_OK);
            }
        }
        lua_pop(L, 1);
        break;

    case WM_MBUTTONDOWN:
        lua_getglobal(L, "down");
        if (lua_isfunction(L, -1))
        {
            lua_pushinteger(L, 3);
            if (lua_pcall(L, 1, 0, 0) != LUA_OK)
            {
                MessageBox(NULL, lua_tostring(L, -1), "Error in Mouse Down", MB_OK);
            }
        }
        lua_pop(L, 1);
        break;

    case WM_MBUTTONUP:
        lua_getglobal(L, "up");
        if (lua_isfunction(L, -1))
        {
            lua_pushinteger(L, 3);
            if (lua_pcall(L, 1, 0, 0) != LUA_OK)
            {
                MessageBox(NULL, lua_tostring(L, -1), "Error in Mouse Up", MB_OK);
            }
        }
        lua_pop(L, 1);
        break;

    case WM_CLOSE:
        running = false;
        lua_getglobal(L, "close");
        if (lua_isfunction(L, -1))
        {
            if (lua_pcall(L, 0, 0, 0) != LUA_OK)
            {
                MessageBox(NULL, lua_tostring(L, -1), "Error in Close :\\", MB_OK);
            }
        }
        lua_pop(L, 1);
        WaitForSingleObject(updateThread, INFINITE);
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_PAINT:
        DrawBuffer(hwnd);
        return 0;
    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}

// PLF Functions
int color_rgb(lua_State *L)
{
    int r = luaL_checkinteger(L, 1);
    int g = luaL_checkinteger(L, 2);
    int b = luaL_checkinteger(L, 3);
    int encodedValue = (r * 64) + (g * 8) + b + 1;
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
        return luaL_error(L, "HSV values must be between 0 and 7 :\\");
    }

    float hue = h / 7.0f * 360.0f;
    float saturation = s / 7.0f;
    float value = v / 7.0f;

    int r, g, b;
    int i = (int)(hue / 60.0f) % 6;
    float f = hue / 60.0f - i;
    float p = value * (1 - saturation);
    float q = value * (1 - f * saturation);
    float t = value * (1 - (1 - f) * saturation);

    switch (i)
    {
    case 0:
        r = value * 7;
        g = t * 7;
        b = p * 7;
        break;
    case 1:
        r = q * 7;
        g = value * 7;
        b = p * 7;
        break;
    case 2:
        r = p * 7;
        g = value * 7;
        b = t * 7;
        break;
    case 3:
        r = p * 7;
        g = q * 7;
        b = value * 7;
        break;
    case 4:
        r = t * 7;
        g = p * 7;
        b = value * 7;
        break;
    case 5:
        r = value * 7;
        g = p * 7;
        b = q * 7;
        break;
    }

    int encodedValue = (r * 64) + (g * 8) + b + 1;
    lua_pushinteger(L, encodedValue);
    return 1;
}

int color_greyscale(lua_State *L)
{
    int color = luaL_checkinteger(L, 1);
    if (color < 1 || color > 512)
    {
        return luaL_error(L, "Color must be between 1 and 512 :\\");
    }

    int encodedValue = color - 1;
    int rIndex = encodedValue / 64;
    int gIndex = (encodedValue % 64) / 8;
    int bIndex = encodedValue % 8;

    int r = rIndex * 36;
    int g = gIndex * 36;
    int b = bIndex * 36;

    int gray = (r + g + b) / 3;
    int grayIndex = gray / 36;
    int greyscaleColor = (grayIndex * 64) + (grayIndex * 8) + grayIndex + 1;

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
            lua_pushvalue(L, 1);
            lua_pushinteger(L, x);
            lua_pushinteger(L, y);
            if (lua_pcall(L, 2, 1, 0) != LUA_OK)
            {
                MessageBox(NULL, lua_tostring(L, -1), "Error in Shader :\\", MB_OK);
                return 0;
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
    // Retrieve the image name (ID) from Lua
    const char *imageName = luaL_checkstring(L, 1);

    // Open the ROM file using the global path
    FILE *file = fopen(romPathGlobal, "rb");
    if (!file)
    {
        return luaL_error(L, "Failed to open ROM file: %s :\\", romPathGlobal);
    }

    // Read the header and validate
    char header[4];
    fread(header, 1, 4, file);
    if (strncmp(header, "imag", 4) != 0)
    {
        fclose(file);
        return luaL_error(L, "Invalid ROM file header :\\");
    }

    // Read the number of images in the ROM
    unsigned char numImages;
    fread(&numImages, 1, 1, file);

    // Search for the target image
    unsigned int numPixels, width, height;
    unsigned short *pixels = NULL;
    bool found = false;
    for (int i = 0; i < numImages; ++i)
    {
        // Read image metadata
        unsigned int numPixelsInImage;
        char imgName[5] = {0}; // 4 chars + null terminator
        fread(&numPixelsInImage, 4, 1, file);
        fread(imgName, 4, 1, file);
        fread(&width, 4, 1, file);
        fread(&height, 4, 1, file);

        // Compare image name
        if (strncmp(imgName, imageName, 4) == 0)
        {
            numPixels = numPixelsInImage;
            if (numPixels != width * height)
            {
                fclose(file);
                return luaL_error(L, "Image size does not match expected dimensions, possible corruption :\\");
            }

            // Sanity check on numPixels
            if (numPixels > 1000000) // Check for realistic pixel count
            {
                fclose(file);
                return luaL_error(L, "Ridiculously large image, I am NOT opening that :\\");
            }

            // Allocate memory for pixels and read them
            pixels = malloc(numPixels * sizeof(unsigned short));
            if (!pixels)
            {
                fclose(file);
                return luaL_error(L, "Failed to allocate memory for image data :\\");
            }
            fread(pixels, sizeof(unsigned short), numPixels, file);
            found = true;
            break;
        }
        else
        {
            // Skip the image data for this image
            fseek(file, numPixelsInImage * sizeof(unsigned short), SEEK_CUR);
        }
    }

    fclose(file);

    if (!found)
    {
        return luaL_error(L, "Image '%s' not found in ROM file :\\", imageName);
    }

    // Create a Lua table to hold the texture
    lua_newtable(L);
    for (int y = 0; y < height; ++y)
    {
        lua_newtable(L);
        for (int x = 0; x < width; ++x)
        {
            int index = y * width + x;
            unsigned short encodedPixel = pixels[index];

            // Push the encoded pixel value into the Lua table
            lua_pushinteger(L, encodedPixel);
            lua_rawseti(L, -2, x + 1);
        }
        lua_rawseti(L, -2, y + 1);
    }

    free(pixels);

    // The Lua table with the texture data is already on top of the stack
    return 1;
}

int drawing_shader(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TFUNCTION);
    for (int y = 0; y < bufferHeight; y++)
    {
        for (int x = 0; x < bufferWidth; x++)
        {
            lua_pushvalue(L, 1);
            lua_pushinteger(L, x);
            lua_pushinteger(L, y);
            if (lua_pcall(L, 2, 1, 0) != LUA_OK)
            {
                MessageBox(NULL, lua_tostring(L, -1), "Error in Shader :\\", MB_OK);
                return 0;
            }
            int value = lua_tointeger(L, -1);
            lua_pop(L, 1);

            if (x >= 0 && x < bufferWidth && y >= 0 && y < bufferHeight)
            {
                if (value <= 0)
                    continue;
                if (value > 512)
                    continue;
                int encodedValue = value - 1;
                int rIndex = encodedValue / 64;
                int gIndex = (encodedValue % 64) / 8;
                int bIndex = encodedValue % 8;

                BYTE r = rIndex * 36;
                BYTE g = gIndex * 36;
                BYTE b = bIndex * 36;

                int index = (y * bufferWidth + x) * 3;
                bitmapData[index] = b;
                bitmapData[index + 1] = g;
                bitmapData[index + 2] = r;
            }
        }
    }
    return 0;
}

int drawing_rect(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    int xOffset = luaL_checkinteger(L, 2);
    int yOffset = luaL_checkinteger(L, 3);
    for (int y = 1; y <= luaL_len(L, 1); y++)
    {
        lua_rawgeti(L, 1, y);
        for (int x = 1; x <= luaL_len(L, -1); x++)
        {
            lua_rawgeti(L, -1, x);
            int value = lua_tointeger(L, -1);
            lua_pop(L, 1);

            int destX = xOffset + x - 1;
            int destY = yOffset + y - 1;

            if (destX >= 0 && destX < bufferWidth && destY >= 0 && destY < bufferHeight)
            {
                if (value <= 0)
                    continue;
                if (value > 512)
                    continue;
                int encodedValue = value - 1;
                int rIndex = encodedValue / 64;
                int gIndex = (encodedValue % 64) / 8;
                int bIndex = encodedValue % 8;

                BYTE r = rIndex * 36;
                BYTE g = gIndex * 36;
                BYTE b = bIndex * 36;

                int index = (destY * bufferWidth + destX) * 3;
                bitmapData[index] = b;
                bitmapData[index + 1] = g;
                bitmapData[index + 2] = r;
            }
        }
        lua_pop(L, 1);
    }
    return 0;
}

int http_get(lua_State *L)
{
    const wchar_t *url = luaL_checkstring(L, 1);
    WCHAR hostname[256] = {0};
    URL_COMPONENTS urlComp;
    memset(&urlComp, 0, sizeof(urlComp));
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.lpszHostName = hostname;
    urlComp.dwHostNameLength = sizeof(hostname) / sizeof(hostname[0]);
    urlComp.dwUrlPathLength = (DWORD)-1; // Automatically set the length

    WinHttpCrackUrl(url, 0, 0, &urlComp);

    HINTERNET hSession = WinHttpOpen(L"A Custom User Agent", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET hConnect = WinHttpConnect(hSession, urlComp.lpszHostName, urlComp.nPort, 0);
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", urlComp.lpszUrlPath, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, urlComp.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);

    BOOL bResults = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);

    if (bResults)
        bResults = WinHttpReceiveResponse(hRequest, NULL);

    DWORD dwStatusCode = 0;
    DWORD dwSize = sizeof(dwStatusCode);

    if (bResults)
    {
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &dwStatusCode, &dwSize, WINHTTP_NO_HEADER_INDEX);
    }

    DWORD dwDownloaded = 0;
    LPSTR pszOutBuffer;
    DWORD dwSizeOut = 0;
    if (bResults)
    {
        do
        {
            // Check how much available data
            dwSizeOut = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &dwSizeOut))
                printf("Error %u in WinHttpQueryDataAvailable.\n", GetLastError());

            // Allocate space for the buffer
            pszOutBuffer = (LPSTR)malloc(dwSizeOut + 1);
            if (!pszOutBuffer)
            {
                printf("Out of memory\n");
                break;
            }
            else
            {
                // Read the data
                ZeroMemory(pszOutBuffer, dwSizeOut + 1);

                if (!WinHttpReadData(hRequest, (LPVOID)pszOutBuffer, dwSizeOut, &dwDownloaded))
                    printf("Error %u in WinHttpReadData.\n", GetLastError());
                else
                {
                    lua_pushlstring(L, pszOutBuffer, dwDownloaded);
                }
                free(pszOutBuffer);
            }
        } while (dwSizeOut > 0);
    }

    // Clean up
    if (hRequest)
        WinHttpCloseHandle(hRequest);
    if (hConnect)
        WinHttpCloseHandle(hConnect);
    if (hSession)
        WinHttpCloseHandle(hSession);

    lua_pushinteger(L, dwStatusCode);
    return 2;
}
int window_message(lua_State *L)
{
    const char *text = luaL_checkstring(L, 1);
    char title[256];
    GetWindowText(hwndGlobal, title, sizeof(title)); // Assuming hwndGlobal is your main window handle
    MessageBox(NULL, text, title, MB_OK);
    return 0;
}

int mouse_position(lua_State *L)
{
    POINT p;
    GetCursorPos(&p); // Get the mouse position in screen coordinates

    // Use WindowFromPoint to ensure you're working with the correct window
    HWND hwndAtPoint = WindowFromPoint(p);
    if (hwndAtPoint != hwndGlobal)
    {
        // The mouse is not over the client area, return nil, nil
        lua_pushnil(L);
        lua_pushnil(L);
        return 2;
    }

    // Convert to client coordinates
    ScreenToClient(hwndGlobal, &p);

    // Get the dimensions of the client area
    RECT clientRect;
    GetClientRect(hwndGlobal, &clientRect);
    int clientWidth = clientRect.right - clientRect.left;
    int clientHeight = clientRect.bottom - clientRect.top;

    // Calculate the aspect ratio of the buffer and the client area
    float bufferAspectRatio = (float)bufferWidth / bufferHeight;
    float clientAspectRatio = (float)clientWidth / clientHeight;

    // Calculate the scaled mouse coordinates
    int bufferX, bufferY;

    if (clientAspectRatio > bufferAspectRatio)
    {
        // Client area is wider than the buffer aspect ratio
        int displayWidth = (int)(clientHeight * bufferAspectRatio);
        int offsetX = (clientWidth - displayWidth) / 2;
        bufferX = (int)((p.x - offsetX) / (float)displayWidth * bufferWidth);
        bufferY = (int)(p.y / (float)clientHeight * bufferHeight);
    }
    else
    {
        // Client area is taller than the buffer aspect ratio
        int displayHeight = (int)(clientWidth / bufferAspectRatio);
        int offsetY = (clientHeight - displayHeight) / 2;
        bufferX = (int)(p.x / (float)clientWidth * bufferWidth);
        bufferY = (int)((p.y - offsetY) / (float)displayHeight * bufferHeight);
    }

    // Clamp the coordinates to the buffer dimensions
    bufferX = max(0, min(bufferX, bufferWidth - 1));
    bufferY = max(0, min(bufferY, bufferHeight - 1));

    // Check if the mouse position is within the valid buffer area
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
    int vkButton;
    switch (button)
    {
    case 1:
        vkButton = VK_LBUTTON;
        break;
    case 2:
        vkButton = VK_RBUTTON;
        break;
    case 3:
        vkButton = VK_MBUTTON;
        break;
    default:
        return luaL_error(L, "Invalid button :\\");
    }
    lua_pushboolean(L, GetAsyncKeyState(vkButton) & 0x8000);
    return 1;
}

// Function to toggle mouse cursor visibility
int mouse_visible(lua_State *L)
{
    bool visible = lua_toboolean(L, 1);
    ShowCursor(visible);
    return 0;
}

// Function to toggle fullscreen
int window_fullscreen(lua_State *L)
{
    bool fullscreen = lua_toboolean(L, 1);
    if (fullscreen && (!isFullscreen))
    {
        // Save the current window position and size
        GetWindowRect(hwndGlobal, &windowRect);

        // Get the dimensions of the primary monitor
        HMONITOR hMonitor = MonitorFromWindow(hwndGlobal, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO monitorInfo = {sizeof(MONITORINFO)};
        GetMonitorInfo(hMonitor, &monitorInfo);

        // Set the window style to borderless and move it to cover the entire monitor
        SetWindowLong(hwndGlobal, GWL_STYLE, WS_POPUP);
        SetWindowPos(hwndGlobal, HWND_TOP, monitorInfo.rcMonitor.left, monitorInfo.rcMonitor.top,
                     monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
                     monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);

        isFullscreen = true;
    }
    else if ((!fullscreen) && isFullscreen)
    {
        // Restore the window style and size
        SetWindowLong(hwndGlobal, GWL_STYLE, WS_OVERLAPPEDWINDOW);
        SetWindowPos(hwndGlobal, HWND_TOP, windowRect.left, windowRect.top,
                     windowRect.right - windowRect.left, windowRect.bottom - windowRect.top,
                     SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);

        isFullscreen = false;
    }
    return 0;
}

// Calculate distance between two points
int util_distance(lua_State *L)
{
    double x1 = luaL_checknumber(L, 1);
    double y1 = luaL_checknumber(L, 2);
    double x2 = luaL_checknumber(L, 3);
    double y2 = luaL_checknumber(L, 4);
    double distance = sqrt(pow(x2 - x1, 2) + pow(y2 - y1, 2));
    lua_pushnumber(L, distance);
    return 1;
}

// Generate a random number within a range or 0-1 if no arguments
int util_random(lua_State *L)
{
    int top = lua_gettop(L);
    if (top == 0)
    {
        lua_pushnumber(L, (double)rand() / RAND_MAX);
    }
    else if (top == 2)
    {
        double min = luaL_checknumber(L, 1);
        double max = luaL_checknumber(L, 2);
        double random = min + (double)rand() / (RAND_MAX / (max - min));
        lua_pushnumber(L, random);
    }
    return 1;
}

// Clamp function
int util_clamp(lua_State *L)
{
    double value = luaL_checknumber(L, 1);
    double min = luaL_checknumber(L, 2);
    double max = luaL_checknumber(L, 3);
    double clamped = value < min ? min : (value > max ? max : value);
    lua_pushnumber(L, clamped);
    return 1;
}

// Linear interpolation function
int util_lerp(lua_State *L)
{
    double start = luaL_checknumber(L, 1);
    double end = luaL_checknumber(L, 2);
    double t = luaL_checknumber(L, 3);
    lua_pushnumber(L, start + t * (end - start));
    return 1;
}

int util_intersect(lua_State *L)
{
    // Get input parameters
    double x1 = luaL_checknumber(L, 1);
    double y1 = luaL_checknumber(L, 2);
    double width1 = luaL_checknumber(L, 3);
    double height1 = luaL_checknumber(L, 4);
    double x2 = luaL_checknumber(L, 5);
    double y2 = luaL_checknumber(L, 6);
    double width2 = luaL_checknumber(L, 7);
    double height2 = luaL_checknumber(L, 8);

    // Calculate the half-widths and half-heights
    double halfWidth1 = width1 / 2.0;
    double halfHeight1 = height1 / 2.0;
    double halfWidth2 = width2 / 2.0;
    double halfHeight2 = height2 / 2.0;

    // Calculate the centers of the rectangles
    double centerX1 = x1 + halfWidth1;
    double centerY1 = y1 + halfHeight1;
    double centerX2 = x2 + halfWidth2;
    double centerY2 = y2 + halfHeight2;

    // Calculate the differences in centers
    double deltaX = centerX2 - centerX1;
    double deltaY = centerY2 - centerY1;

    // Calculate the combined half-widths and half-heights
    double combinedHalfWidth = halfWidth1 + halfWidth2;
    double combinedHalfHeight = halfHeight1 + halfHeight2;

    // Check for collision
    if (fabs(deltaX) < combinedHalfWidth && fabs(deltaY) < combinedHalfHeight)
    {
        // Collision detected, calculate the overlaps
        double overlapX = combinedHalfWidth - fabs(deltaX);
        double overlapY = combinedHalfHeight - fabs(deltaY);

        // Determine the minimal movement needed to resolve the collision
        if (overlapX < overlapY)
        {
            // Resolve collision in X direction
            if (deltaX > 0)
            {
                // Move rect2 to the right
                lua_pushnumber(L, overlapX);  // x1 movement
                lua_pushnumber(L, 0);         // y1 movement
                lua_pushnumber(L, -overlapX); // x2 movement
                lua_pushnumber(L, 0);         // y2 movement
            }
            else
            {
                // Move rect2 to the left
                lua_pushnumber(L, -overlapX); // x1 movement
                lua_pushnumber(L, 0);         // y1 movement
                lua_pushnumber(L, overlapX);  // x2 movement
                lua_pushnumber(L, 0);         // y2 movement
            }
        }
        else
        {
            // Resolve collision in Y direction
            if (deltaY > 0)
            {
                // Move rect2 down
                lua_pushnumber(L, 0);         // x1 movement
                lua_pushnumber(L, overlapY);  // y1 movement
                lua_pushnumber(L, 0);         // x2 movement
                lua_pushnumber(L, -overlapY); // y2 movement
            }
            else
            {
                // Move rect2 up
                lua_pushnumber(L, 0);         // x1 movement
                lua_pushnumber(L, -overlapY); // y1 movement
                lua_pushnumber(L, 0);         // x2 movement
                lua_pushnumber(L, overlapY);  // y2 movement
            }
        }
        return 4; // Return 4 values (movements)
    }
    else
    {
        // No collision
        lua_pushnumber(L, 0); // x1 movement
        lua_pushnumber(L, 0); // y1 movement
        lua_pushnumber(L, 0); // x2 movement
        lua_pushnumber(L, 0); // y2 movement
        return 4;             // Return 4 values (all zeros)
    }
}

int keyboard_down(lua_State *L)
{
    const char *key = luaL_checkstring(L, 1);
    SHORT vkey;

    if (strlen(key) == 1) // Single character keys (e.g., "a", "B", etc.)
    {
        char lowercaseKey = tolower(key[0]);
        vkey = VkKeyScanA(lowercaseKey);
    }
    else
    {
        // Handle direct VK_ constants
        if (strcmp(key, "enter") == 0)
            vkey = VK_RETURN;
        else if (strcmp(key, "shift") == 0)
            vkey = VK_SHIFT;
        else if (strcmp(key, "control") == 0)
            vkey = VK_CONTROL;
        else if (strcmp(key, "alt") == 0)
            vkey = VK_MENU;
        else if (strcmp(key, "escape") == 0)
            vkey = VK_ESCAPE;
        else if (strcmp(key, "back") == 0)
            vkey = VK_BACK;
        else if (strcmp(key, "tab") == 0)
            vkey = VK_TAB;
        else if (strcmp(key, "left") == 0)
            vkey = VK_LEFT;
        else if (strcmp(key, "right") == 0)
            vkey = VK_RIGHT;
        else if (strcmp(key, "up") == 0)
            vkey = VK_UP;
        else if (strcmp(key, "down") == 0)
            vkey = VK_DOWN;
        // Add more VK_ mappings as needed
        else
            return luaL_error(L, "Unrecognized key: %s :\\", key);
    }

    lua_pushboolean(L, GetAsyncKeyState(vkey) & 0x8000);
    return 1;
}

int window_title(lua_State *L)
{
    const char *title = luaL_checkstring(L, 1);
    SetWindowText(hwndGlobal, title);
    return 0;
}

int window_close(lua_State *L)
{
    PostMessage(hwndGlobal, WM_CLOSE, 0, 0);
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    const char CLASS_NAME[] = "PLF Window Class";
    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;

    RegisterClass(&wc);
    HWND hwnd = CreateWindowEx(0, CLASS_NAME, "PLF Window", WS_OVERLAPPEDWINDOW,
                               CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                               NULL, NULL, hInstance, NULL);
    hwndGlobal = hwnd;

    if (hwnd == NULL)
    {
        MessageBox(NULL, "HWND is null", "Error :\\", MB_OK);
        return 0;
    }

    if (__argc < 3) // Expecting at least two command line arguments
    {
        MessageBox(NULL, "Usage: <program> <script_path> <rom_path>", "Error :\\", MB_OK);
        return 0;
    }

    // Get the script path and ROM path from command line arguments
    const char *scriptPath = __argv[1];
    romPathGlobal = __argv[2]; // Store ROM path in global variable

    ShowWindow(hwnd, nCmdShow);
    HDC hdc = GetDC(hwnd);

    InitializeLua(scriptPath);

    if (!L)
    {
        return 0;
    }

    lua_getglobal(L, "width");
    if (!lua_isinteger(L, -1))
    {
        MessageBox(NULL, "Expected integer for 'width'", "Error :\\", MB_OK);
        lua_close(L);
        return 0;
    }
    bufferWidth = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);

    lua_getglobal(L, "height");
    if (!lua_isinteger(L, -1))
    {
        MessageBox(NULL, "Expected integer for 'height'", "Error :\\", MB_OK);
        lua_close(L);
        return 0;
    }
    bufferHeight = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);

    lua_getglobal(L, "dt");
    if (!lua_isnumber(L, -1))
    {
        MessageBox(NULL, "Expected number for 'dt'", "Error :\\", MB_OK);
        lua_close(L);
        return 0;
    }
    updateInterval = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);

    lua_getglobal(L, "title");
    if (lua_isstring(L, -1))
    {
        SetWindowText(hwnd, lua_tostring(L, -1));
    }
    lua_pop(L, 1);

    lua_getglobal(L, "load");
    if (lua_isfunction(L, -1))
    {
        if (lua_pcall(L, 0, 0, 0) != LUA_OK)
        {
            MessageBox(NULL, lua_tostring(L, -1), "Error in Load :\\", MB_OK);
            lua_close(L);
            return 0;
        }
    }
    lua_pop(L, 1);

    SetupBuffer(hdc, bufferWidth, bufferHeight);
    ReleaseDC(hwnd, hdc);

    updateThread = (HANDLE)_beginthreadex(NULL, 0, LuaUpdateThread, NULL, 0, NULL);

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    running = false;
    WaitForSingleObject(updateThread, INFINITE);
    CloseHandle(updateThread);

    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    lua_close(L);

    return (int)msg.wParam;
}
