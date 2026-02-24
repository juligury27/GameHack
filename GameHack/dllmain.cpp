// ============================================================================
// GAMEHACK DLL — Project 5: ESP + Aimbot
//
// ESP: Hooks OpenGL to draw boxes, names, health bars on enemies.
// Aimbot: Calculates the angle from camera to enemy head and writes
//         it to the player's yaw/pitch, so bullets always hit the head.
//
// Aimbot logic (reverse of WorldToScreen):
//   1. Get vector from our camera to enemy head
//   2. Convert that vector to yaw/pitch angles using atan2
//   3. Smoothly interpolate from current angles toward target angles
//   4. Write the new angles to the local player's yaw/pitch in memory
// ============================================================================

#include "pch.h"
#include <iostream>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <GL/gl.h>

// We need wglSwapBuffers from opengl32.dll — declare the function pointer type
typedef BOOL(WINAPI* tWglSwapBuffers)(HDC hdc);
tWglSwapBuffers oWglSwapBuffers = nullptr;  // pointer to the ORIGINAL function

// ============================================================================
// AC 1.3.0.2 OFFSETS (confirmed by us)
// ============================================================================
const uintptr_t OFFSET_LOCAL_PLAYER = 0x17E0A8;
const uintptr_t OFFSET_ENTITY_LIST  = 0x191FCC;

const uintptr_t PLAYER_HEAD_X  = 0x04;
const uintptr_t PLAYER_HEAD_Y  = 0x08;
const uintptr_t PLAYER_HEAD_Z  = 0x0C;
const uintptr_t PLAYER_POS_X   = 0x28;
const uintptr_t PLAYER_POS_Y   = 0x2C;
const uintptr_t PLAYER_POS_Z   = 0x30;
const uintptr_t PLAYER_YAW     = 0x34;
const uintptr_t PLAYER_PITCH   = 0x38;
const uintptr_t PLAYER_HEALTH  = 0xEC;
const uintptr_t PLAYER_NAME    = 0x205;
const uintptr_t PLAYER_TEAM    = 0x30C;  // byte: 0=CLA, 1=RVSF (confirmed by scan)

// ============================================================================
// Settings — toggled with keyboard
// ============================================================================

// ESP
bool g_espEnabled    = true;
bool g_espBoxes      = true;
bool g_espNames      = true;
bool g_espHealth     = true;
bool g_espSnapLines  = true;

// Aimbot
bool g_aimbotEnabled = false;   // off by default — toggle with F5
float g_aimbotFOV    = 30.0f;   // only aim at enemies within 30° of crosshair
float g_aimbotSmooth = 5.0f;    // smoothing factor (1 = instant snap, higher = smoother)
bool g_aimbotVisuals = true;    // draw FOV circle on screen

// Cached base address
uintptr_t g_baseAddress = 0;

// ============================================================================
// SafeRead / IsValidPtr (same as before)
// ============================================================================
template <typename T>
bool SafeRead(uintptr_t address, T& outValue)
{
    __try
    {
        outValue = *(T*)address;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool SafeReadRawString(uintptr_t address, char* outBuf, int maxLen)
{
    __try
    {
        char* str = (char*)address;
        int i = 0;
        for (; i < maxLen - 1 && str[i] != '\0'; i++)
        {
            if (str[i] >= 32 && str[i] < 127)
                outBuf[i] = str[i];
            else
                break;
        }
        outBuf[i] = '\0';
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        outBuf[0] = '\0';
        return false;
    }
}

bool IsValidPtr(uintptr_t addr)
{
    return (addr > 0x10000 && addr < 0x7FFFFFFF);
}

// SafeWrite — write a value to a game memory address
template <typename T>
bool SafeWrite(uintptr_t address, T value)
{
    __try
    {
        *(T*)address = value;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// ============================================================================
// WorldToScreen — The core math behind ESP
//
// WHY WE DON'T USE OPENGL MATRICES:
//   When wglSwapBuffers is called, the game has already finished rendering
//   the 3D scene AND drawn the HUD. So the modelview/projection matrices
//   at that point are set up for 2D HUD drawing, not 3D perspective.
//   Reading them with glGetFloatv would give us useless HUD matrices.
//
// SOLUTION: Build our own projection using the player's view angles.
//   We know the camera position (player's head), yaw, pitch, and FOV.
//   From yaw/pitch we compute the camera's forward/right/up vectors.
//   Then for any 3D world point, we:
//     1. Compute the vector from camera to that point
//     2. Project it onto the camera's right and up axes (gives screen offset)
//     3. Divide by the forward component (perspective divide — farther = smaller)
//     4. Scale by FOV to get pixel coordinates
//
// Camera basis vectors are derived from the Cube engine's OpenGL transform:
//   glRotatef(pitch, -1, 0, 0)  →  pitch rotation
//   glRotatef(yaw, 0, 1, 0)     →  yaw rotation
//   glTranslatef(-x, -z, -y)    →  Cube uses (x,y)=horizontal, z=up
//                                   but OpenGL uses Y=up, so Z and Y are swapped
//
// This gives us (in Cube world coordinates):
//   Forward = (sin(yaw)*cos(pitch), -cos(yaw)*cos(pitch), sin(pitch))
//   Right   = (cos(yaw), sin(yaw), 0)
//   Up      = (sin(pitch)*sin(yaw), -sin(pitch)*cos(yaw), cos(pitch))
// ============================================================================
const float PI = 3.14159265358979f;

bool WorldToScreen(
    float targetX, float targetY, float targetZ,   // world position of the target
    float camX, float camY, float camZ,             // camera (head) position
    float yaw, float pitch,                         // view angles in degrees
    int screenW, int screenH,                        // screen dimensions in pixels
    float fov,                                       // horizontal FOV in degrees
    float& outX, float& outY)                       // output: screen pixel coordinates
{
    // Vector from camera to target
    float dx = targetX - camX;
    float dy = targetY - camY;
    float dz = targetZ - camZ;

    // Convert angles to radians
    float yr = yaw * (PI / 180.0f);
    float pr = pitch * (PI / 180.0f);

    float sy = sinf(yr), cy = cosf(yr);
    float sp = sinf(pr), cp = cosf(pr);

    // Project the delta vector onto the camera's three axes:
    //   dotFwd   = how far in front of us (depth)
    //   dotRight = how far to our right
    //   dotUp    = how far above us
    //
    // Derived from Cube engine's camera transform:
    //   glRotatef(pitch, -1, 0, 0) then glRotatef(yaw, 0, 1, 0)
    //   with GL coordinate mapping: GL_X=WorldX, GL_Y=WorldZ, GL_Z=WorldY
    //
    //   Forward = (sin(y)*cos(p), -cos(y)*cos(p),  sin(p))
    //   Right   = (cos(y),         sin(y),          0     )
    //   Up      = (-sin(p)*sin(y), sin(p)*cos(y),   cos(p))
    float dotFwd   = dx * (sy * cp)   + dy * (-cy * cp)  + dz * sp;
    float dotRight = dx * cy           + dy * sy;
    float dotUp    = dx * (-sp * sy)   + dy * (sp * cy)   + dz * cp;

    // If the target is behind the camera, don't draw
    if (dotFwd < 0.1f)
        return false;

    // Perspective projection scale factor
    // This converts from "world units at distance dotFwd" to screen pixels.
    // tan(fov/2) = half-screen-width-in-world / distance, so:
    // pixels-from-center = (world-offset / distance) * (screenW/2) / tan(fov/2)
    float scale = (screenW / 2.0f) / tanf(fov * 0.5f * PI / 180.0f);

    // Final screen coordinates
    // Our glOrtho is set up with (0,0) at bottom-left, so Y-up = positive
    outX = (screenW / 2.0f) + (dotRight / dotFwd) * scale;
    outY = (screenH / 2.0f) + (dotUp / dotFwd) * scale;

    return true;
}

// ============================================================================
// OpenGL drawing helpers — draw basic shapes in 2D screen space
// ============================================================================

// Setup 2D drawing mode (orthographic projection matching screen pixels)
void Begin2D()
{
    int viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    // Map coordinates to screen pixels: (0,0) = bottom-left
    glOrtho(0, viewport[2], 0, viewport[3], -1, 1);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    // Disable things that would interfere with our 2D drawing
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

// Restore the game's original rendering state
void End2D()
{
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);

    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
}

// Draw a 2D outlined rectangle (box)
void DrawBox(float x, float y, float w, float h, float r, float g, float b, float a = 1.0f)
{
    glLineWidth(2.0f);
    glColor4f(r, g, b, a);
    glBegin(GL_LINE_LOOP);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();
}

// Draw a filled rectangle (for health bars)
void DrawFilledBox(float x, float y, float w, float h, float r, float g, float b, float a = 1.0f)
{
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();
}

// Draw a line between two screen points
void DrawLine(float x1, float y1, float x2, float y2, float r, float g, float b, float a = 1.0f)
{
    glLineWidth(1.5f);
    glColor4f(r, g, b, a);
    glBegin(GL_LINES);
    glVertex2f(x1, y1);
    glVertex2f(x2, y2);
    glEnd();
}

// Draw a circle outline (for FOV circle and head dots)
void DrawCircle(float cx, float cy, float radius, int segments, float r, float g, float b, float a = 1.0f)
{
    glLineWidth(1.5f);
    glColor4f(r, g, b, a);
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < segments; i++)
    {
        float angle = 2.0f * PI * i / segments;
        glVertex2f(cx + cosf(angle) * radius, cy + sinf(angle) * radius);
    }
    glEnd();
}

// Draw a filled circle (for head dot)
void DrawFilledCircle(float cx, float cy, float radius, int segments, float r, float g, float b, float a = 1.0f)
{
    glColor4f(r, g, b, a);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);
    for (int i = 0; i <= segments; i++)
    {
        float angle = 2.0f * PI * i / segments;
        glVertex2f(cx + cosf(angle) * radius, cy + sinf(angle) * radius);
    }
    glEnd();
}

// ============================================================================
// Bitmap font system — uses Windows GDI to create OpenGL display lists
//
// wglUseFontBitmaps creates 256 display lists, one per ASCII character.
// Each list contains a bitmap rendering of that character. When we call
// glCallLists with a string, OpenGL renders each character in sequence.
// ============================================================================
GLuint g_fontBase = 0;      // base display list ID for our font
bool g_fontInitialized = false;

void InitFont(HDC hdc)
{
    g_fontBase = glGenLists(256);

    // Create a simple fixed-width font
    HFONT font = CreateFontA(
        14,             // height in pixels
        0,              // width (0 = auto)
        0, 0,           // escapement, orientation
        FW_BOLD,        // weight (bold so it's readable)
        FALSE, FALSE, FALSE,  // italic, underline, strikeout
        ANSI_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY,
        FF_DONTCARE | DEFAULT_PITCH,
        "Consolas"      // font face — monospace, clean look
    );

    HFONT oldFont = (HFONT)SelectObject(hdc, font);
    wglUseFontBitmaps(hdc, 0, 256, g_fontBase);
    SelectObject(hdc, oldFont);
    DeleteObject(font);

    g_fontInitialized = true;
}

void DrawText2D(float x, float y, const char* text, float r, float g, float b)
{
    if (!g_fontInitialized || !text || !text[0])
        return;

    glColor3f(r, g, b);
    glRasterPos2f(x, y);

    // Push the list base so we can use glCallLists with raw ASCII bytes
    glPushAttrib(GL_LIST_BIT);
    glListBase(g_fontBase);
    glCallLists((GLsizei)strlen(text), GL_UNSIGNED_BYTE, text);
    glPopAttrib();
}

// ============================================================================
// CalcAngle — The core aimbot math (reverse of WorldToScreen)
//
// Given our camera position and a target position, calculate what yaw
// and pitch angles we'd need to look directly at the target.
//
// This is essentially the inverse of our W2S forward vector:
//   Forward = (sin(yaw)*cos(pitch), -cos(yaw)*cos(pitch), sin(pitch))
//
// Given delta = target - camera, we solve for yaw and pitch:
//   yaw   = atan2(dx, -dy)     (horizontal angle)
//   pitch = atan2(dz, sqrt(dx²+dy²))  (vertical angle)
// ============================================================================
void CalcAngle(float camX, float camY, float camZ,
               float targetX, float targetY, float targetZ,
               float& outYaw, float& outPitch)
{
    float dx = targetX - camX;
    float dy = targetY - camY;
    float dz = targetZ - camZ;

    float horizontalDist = sqrtf(dx * dx + dy * dy);

    // atan2(sin_component, cos_component) gives us the angle
    // From our forward vector: forward.x = sin(yaw)*cos(pitch)
    //                          forward.y = -cos(yaw)*cos(pitch)
    // So: sin(yaw) ∝ dx, cos(yaw) ∝ -dy → yaw = atan2(dx, -dy)
    outYaw = atan2f(dx, -dy) * (180.0f / PI);
    outPitch = atan2f(dz, horizontalDist) * (180.0f / PI);
}

// ============================================================================
// NormalizeAngle — Keep angle in [-180, 180] range
// When smoothing between angles, we need to handle the 360° wraparound.
// Example: current=350°, target=10° → difference should be +20°, not -340°
// ============================================================================
float NormalizeAngle(float angle)
{
    while (angle > 180.0f) angle -= 360.0f;
    while (angle < -180.0f) angle += 360.0f;
    return angle;
}

// ============================================================================
// RunAimbot — Find best target and adjust our view angles
//
// Target selection: "closest to crosshair" — picks the enemy whose head
// is nearest to the center of your screen. This feels the most natural
// because you roughly aim near someone and the aimbot locks on.
//
// Smooth aim: Instead of instantly snapping (obvious and robotic), we
// move a fraction of the distance each frame. With smoothing=5, we move
// 1/5th of the remaining angle per frame → fast but not instant.
//
// FOV limit: Only target enemies within N degrees of crosshair. This
// prevents the aimbot from doing a 180° turn, which looks inhuman.
// ============================================================================
void RunAimbot(uintptr_t localPlayerAddr, uintptr_t entityListAddr,
               float camX, float camY, float camZ, float yaw, float pitch,
               BYTE localTeam)
{
    if (!g_aimbotEnabled)
        return;

    // Only aim while holding right mouse button (aim key)
    if (!(GetAsyncKeyState(VK_RBUTTON) & 0x8000))
        return;

    float bestAngleDist = g_aimbotFOV;  // only consider targets within FOV
    float bestYaw = 0, bestPitch = 0;
    bool foundTarget = false;

    for (int i = 0; i < 32; i++)
    {
        uintptr_t entityAddr = 0;
        if (!SafeRead<uintptr_t>(entityListAddr + (i * 4), entityAddr)
            || !IsValidPtr(entityAddr))
            continue;

        if (entityAddr == localPlayerAddr)
            continue;

        int health = 0;
        if (!SafeRead<int>(entityAddr + PLAYER_HEALTH, health) || health <= 0)
            continue;

        BYTE entityTeam = 0;
        SafeRead<BYTE>(entityAddr + PLAYER_TEAM, entityTeam);
        if (entityTeam == localTeam)
            continue;

        // Read enemy head position (feet + height for reliability)
        float feetX = 0, feetY = 0, feetZ = 0;
        SafeRead<float>(entityAddr + PLAYER_POS_X, feetX);
        SafeRead<float>(entityAddr + PLAYER_POS_Y, feetY);
        SafeRead<float>(entityAddr + PLAYER_POS_Z, feetZ);

        // Aim at head center (player is ~5.0 tall: eyeheight=4.5 + aboveeye=0.5)
        // Head center is between eye level and skull top: ~4.75 above feet
        float headZ = feetZ + 4.75f;

        // Calculate the angle we'd need to look at this enemy's head
        float targetYaw = 0, targetPitch = 0;
        CalcAngle(camX, camY, camZ, feetX, feetY, headZ, targetYaw, targetPitch);

        // How far is this target from our current crosshair? (in degrees)
        float deltaYaw = NormalizeAngle(targetYaw - yaw);
        float deltaPitch = NormalizeAngle(targetPitch - pitch);
        float angleDist = sqrtf(deltaYaw * deltaYaw + deltaPitch * deltaPitch);

        // Pick the target closest to our crosshair
        if (angleDist < bestAngleDist)
        {
            bestAngleDist = angleDist;
            bestYaw = targetYaw;
            bestPitch = targetPitch;
            foundTarget = true;
        }
    }

    if (!foundTarget)
        return;

    // Smooth aim with adaptive smoothing:
    //   - When angle difference is large (close range, far off target): use full smoothing
    //   - When angle difference is small (far range, almost on target): reduce smoothing
    //   - When very close (< 0.5°): snap directly to avoid endless creeping
    //
    // This fixes the "inaccurate at long range" problem. At distance, the angle
    // to the head is tiny. With fixed smoothing=5, we'd move only 1/5th of a tiny
    // angle each frame, never converging. Adaptive smoothing uses less smoothing
    // for small corrections, so we lock on precisely.
    float deltaYaw = NormalizeAngle(bestYaw - yaw);
    float deltaPitch = NormalizeAngle(bestPitch - pitch);
    float totalDelta = sqrtf(deltaYaw * deltaYaw + deltaPitch * deltaPitch);

    float newYaw, newPitch;
    if (totalDelta < 0.5f)
    {
        // Very close to target — snap directly for precision
        newYaw = bestYaw;
        newPitch = bestPitch;
    }
    else
    {
        // Adaptive: smoothing scales down as we get closer to the target
        // At 10°+ away: full smoothing. At 1° away: almost instant.
        float adaptiveSmooth = 1.0f + (g_aimbotSmooth - 1.0f) * (totalDelta / g_aimbotFOV);
        if (adaptiveSmooth < 1.5f) adaptiveSmooth = 1.5f;  // minimum smoothing to not look robotic

        newYaw = yaw + deltaYaw / adaptiveSmooth;
        newPitch = pitch + deltaPitch / adaptiveSmooth;
    }

    // Write the new angles to our player struct
    SafeWrite<float>(localPlayerAddr + PLAYER_YAW, newYaw);
    SafeWrite<float>(localPlayerAddr + PLAYER_PITCH, newPitch);
}

// ============================================================================
// DrawESP + Aimbot visuals — Called every frame
// ============================================================================

static bool g_debugPrinted = false;

void DrawESP()
{
    if (!g_espEnabled || g_baseAddress == 0)
        return;

    // Read local player pointer
    uintptr_t localPlayerAddr = 0;
    if (!SafeRead<uintptr_t>(g_baseAddress + OFFSET_LOCAL_PLAYER, localPlayerAddr)
        || !IsValidPtr(localPlayerAddr))
        return;

    // Read entity list pointer
    uintptr_t entityListAddr = 0;
    if (!SafeRead<uintptr_t>(g_baseAddress + OFFSET_ENTITY_LIST, entityListAddr)
        || !IsValidPtr(entityListAddr))
        return;

    // Read camera data from local player (head position = eye/camera)
    float camX = 0, camY = 0, camZ = 0;
    float yaw = 0, pitch = 0;
    SafeRead<float>(localPlayerAddr + PLAYER_HEAD_X, camX);
    SafeRead<float>(localPlayerAddr + PLAYER_HEAD_Y, camY);
    SafeRead<float>(localPlayerAddr + PLAYER_HEAD_Z, camZ);
    SafeRead<float>(localPlayerAddr + PLAYER_YAW, yaw);
    SafeRead<float>(localPlayerAddr + PLAYER_PITCH, pitch);

    // Read local player's team value for team check
    BYTE localTeam = 0;
    SafeRead<BYTE>(localPlayerAddr + PLAYER_TEAM, localTeam);

    // Get screen dimensions from OpenGL viewport
    int viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    int screenW = viewport[2];
    int screenH = viewport[3];
    float screenCenterX = screenW / 2.0f;

    // Horizontal FOV — AssaultCube default is 90 degrees
    float fov = 90.0f;

    if (!g_debugPrinted)
    {
        std::cout << "[+] Your team: " << (int)localTeam << " (ESP shows enemies only)\n\n";
        g_debugPrinted = true;
    }

    // Run aimbot logic (writes to player yaw/pitch if active)
    RunAimbot(localPlayerAddr, entityListAddr, camX, camY, camZ, yaw, pitch, localTeam);

    Begin2D();

    // --- Draw custom crosshair at screen center ---
    float cx = screenW / 2.0f;
    float cy = screenH / 2.0f;
    float crossSize = 6.0f;
    DrawLine(cx - crossSize, cy, cx + crossSize, cy, 0.0f, 1.0f, 0.0f, 0.9f);
    DrawLine(cx, cy - crossSize, cx, cy + crossSize, 0.0f, 1.0f, 0.0f, 0.9f);

    // --- Draw aimbot FOV circle ---
    if (g_aimbotEnabled && g_aimbotVisuals)
    {
        // Convert FOV degrees to screen pixels for the circle radius
        float fovScale = (screenW / 2.0f) / tanf(fov * 0.5f * PI / 180.0f);
        float fovRadius = tanf(g_aimbotFOV * PI / 180.0f) * fovScale;
        DrawCircle(cx, cy, fovRadius, 48, 1.0f, 1.0f, 1.0f, 0.3f);
    }

    // Loop through entities (up to 32 max)
    for (int i = 0; i < 32; i++)
    {
        uintptr_t entityAddr = 0;
        if (!SafeRead<uintptr_t>(entityListAddr + (i * 4), entityAddr)
            || !IsValidPtr(entityAddr))
            continue;  // skip empty slots

        // Skip self
        if (entityAddr == localPlayerAddr)
            continue;

        // Read entity data
        int health = 0;
        if (!SafeRead<int>(entityAddr + PLAYER_HEALTH, health))
            continue;

        // Skip dead players
        if (health <= 0)
            continue;

        // Team check — skip teammates (same team byte value)
        BYTE entityTeam = 0;
        SafeRead<BYTE>(entityAddr + PLAYER_TEAM, entityTeam);
        if (entityTeam == localTeam)
            continue;  // same team = teammate, skip

        // Read feet position (always updated reliably, even behind walls)
        float feetX = 0, feetY = 0, feetZ = 0;
        SafeRead<float>(entityAddr + PLAYER_POS_X, feetX);
        SafeRead<float>(entityAddr + PLAYER_POS_Y, feetY);
        SafeRead<float>(entityAddr + PLAYER_POS_Z, feetZ);

        // Full player height in AC: eyeheight (4.5) + aboveeye (0.5) = 5.0
        // The head offset at +0x04/08/0C is an interpolated render position that
        // can go stale when enemies aren't visible. Using feet + height is more reliable.
        float playerHeight = 5.0f;
        float headX = feetX;
        float headY = feetY;
        float headZ = feetZ + playerHeight;

        // World-to-screen for head and feet using our manual projection
        float headScreenX, headScreenY;
        float feetScreenX, feetScreenY;

        if (!WorldToScreen(headX, headY, headZ,
                           camX, camY, camZ, yaw, pitch,
                           screenW, screenH, fov,
                           headScreenX, headScreenY))
            continue;  // behind camera

        if (!WorldToScreen(feetX, feetY, feetZ,
                           camX, camY, camZ, yaw, pitch,
                           screenW, screenH, fov,
                           feetScreenX, feetScreenY))
            continue;

        // Calculate box dimensions from head/feet screen positions
        float boxHeight = fabs(headScreenY - feetScreenY);
        float boxWidth = boxHeight * 0.5f;  // rough aspect ratio for a person
        float boxX = headScreenX - boxWidth / 2.0f;
        float boxY = feetScreenY;  // bottom of box at feet (lower Y = lower on screen)

        // Make sure the box has minimum visible size
        if (boxHeight < 5.0f)
            continue;

        // Choose color based on health (green = healthy, red = low HP)
        float colorR = 1.0f - (health / 100.0f);
        float colorG = health / 100.0f;
        float colorB = 0.0f;

        // --- Draw box ---
        if (g_espBoxes)
        {
            DrawBox(boxX, boxY, boxWidth, boxHeight, colorR, colorG, colorB);
        }

        // --- Draw head dot right after box (before text to avoid GL state issues) ---
        // Bright magenta 10x10 square centered on head position
        DrawFilledBox(headScreenX - 5.0f, headScreenY - 5.0f, 10.0f, 10.0f, 1.0f, 0.0f, 1.0f, 1.0f);

        // --- Draw health bar (left side of box) ---
        if (g_espHealth)
        {
            float healthPercent = health / 100.0f;
            if (healthPercent > 1.0f) healthPercent = 1.0f;
            float barWidth = 3.0f;
            float barHeight = boxHeight * healthPercent;

            // Background (dark)
            DrawFilledBox(boxX - barWidth - 2, boxY, barWidth, boxHeight, 0.0f, 0.0f, 0.0f, 0.5f);
            // Health fill
            DrawFilledBox(boxX - barWidth - 2, boxY, barWidth, barHeight, colorR, colorG, 0.0f, 0.8f);
        }

        // --- Draw snap line (from bottom center of screen to feet) ---
        if (g_espSnapLines)
        {
            DrawLine(screenCenterX, 0.0f, feetScreenX, feetScreenY, 1.0f, 1.0f, 1.0f, 0.4f);
        }

        // --- Draw name and health text above the box ---
        if (g_espNames)
        {
            char nameBuf[64] = {0};
            SafeReadRawString(entityAddr + PLAYER_NAME, nameBuf, 32);

            char label[96] = {0};
            if (nameBuf[0])
                sprintf_s(label, "%s [%d]", nameBuf, health);
            else
                sprintf_s(label, "??? [%d]", health);

            float textWidth = strlen(label) * 8.0f;
            DrawText2D(headScreenX - textWidth / 2.0f, headScreenY + 10, label, colorR, colorG, colorB);
        }

    }

    End2D();
}

// Forward declaration (needed because CallOriginalSwapBuffers references hkWglSwapBuffers)
BOOL WINAPI hkWglSwapBuffers(HDC hdc);

// ============================================================================
// Hook system — "Unhook-Call-Rehook" method
//
// The trampoline approach (copy first N bytes + JMP back) is fragile because
// the copied instructions may contain relative offsets that break when moved
// to a different address. Instead, we use a simpler method:
//
//   1. Save the first 5 bytes of wglSwapBuffers
//   2. Overwrite them with a JMP to our hook
//   3. When our hook needs to call the REAL wglSwapBuffers:
//      a. Temporarily restore the original 5 bytes
//      b. Call the real function
//      c. Re-install our JMP patch
//
// This is safe for OpenGL because the renderer is single-threaded — only
// one thread ever calls wglSwapBuffers, so there's no race condition during
// the brief moment the original bytes are restored.
// ============================================================================

BYTE g_originalBytes[5] = {0};   // saved original first 5 bytes
void* g_hookTarget = nullptr;     // address of wglSwapBuffers
bool g_hookInstalled = false;

// Temporarily restore original bytes, call real function, re-patch
BOOL CallOriginalSwapBuffers(HDC hdc)
{
    // Restore original bytes
    DWORD oldProtect;
    VirtualProtect(g_hookTarget, 5, PAGE_EXECUTE_READWRITE, &oldProtect);
    memcpy(g_hookTarget, g_originalBytes, 5);
    VirtualProtect(g_hookTarget, 5, oldProtect, &oldProtect);

    // Call the real function (now that it's unpatched)
    tWglSwapBuffers realFunc = (tWglSwapBuffers)g_hookTarget;
    BOOL result = realFunc(hdc);

    // Re-install our hook
    VirtualProtect(g_hookTarget, 5, PAGE_EXECUTE_READWRITE, &oldProtect);
    BYTE* target = (BYTE*)g_hookTarget;
    target[0] = 0xE9;  // JMP rel32
    *(DWORD*)(target + 1) = (DWORD)((uintptr_t)hkWglSwapBuffers - (uintptr_t)g_hookTarget - 5);
    VirtualProtect(g_hookTarget, 5, oldProtect, &oldProtect);

    return result;
}

// ============================================================================
// Hooked wglSwapBuffers — This runs EVERY FRAME
//
// wglSwapBuffers is the OpenGL function that presents the rendered frame
// to the screen. By hooking it, we can draw our ESP after the game has
// finished rendering but before the frame is shown to the player.
//
// Flow: Game renders → our hook draws ESP on top → frame is displayed
// ============================================================================
BOOL WINAPI hkWglSwapBuffers(HDC hdc)
{
    // Initialize our bitmap font on the first frame (needs a valid HDC)
    if (!g_fontInitialized)
        InitFont(hdc);

    // Draw our ESP overlay
    DrawESP();

    // Call the original function using unhook-call-rehook
    return CallOriginalSwapBuffers(hdc);
}

bool InstallHook()
{
    // Get the address of wglSwapBuffers in opengl32.dll
    HMODULE hOpenGL = GetModuleHandleA("opengl32.dll");
    if (!hOpenGL)
    {
        std::cout << "[ERROR] opengl32.dll not loaded!\n";
        return false;
    }

    g_hookTarget = (void*)GetProcAddress(hOpenGL, "wglSwapBuffers");
    if (!g_hookTarget)
    {
        std::cout << "[ERROR] Could not find wglSwapBuffers!\n";
        return false;
    }

    std::cout << "[+] wglSwapBuffers at: 0x" << std::hex << (uintptr_t)g_hookTarget << std::dec << "\n";

    // Save original first 5 bytes (we'll need them to call the real function)
    memcpy(g_originalBytes, g_hookTarget, 5);

    // Patch the function: overwrite first 5 bytes with JMP to our hook
    DWORD oldProtect;
    VirtualProtect(g_hookTarget, 5, PAGE_EXECUTE_READWRITE, &oldProtect);

    BYTE* target = (BYTE*)g_hookTarget;
    target[0] = 0xE9;  // JMP rel32 (5-byte relative jump)
    *(DWORD*)(target + 1) = (DWORD)((uintptr_t)hkWglSwapBuffers - (uintptr_t)g_hookTarget - 5);

    VirtualProtect(g_hookTarget, 5, oldProtect, &oldProtect);

    g_hookInstalled = true;
    std::cout << "[+] wglSwapBuffers hooked successfully!\n";
    return true;
}

// Restore original bytes to unhook permanently (for eject)
void RemoveHook()
{
    if (g_hookTarget && g_hookInstalled)
    {
        DWORD oldProtect;
        VirtualProtect(g_hookTarget, 5, PAGE_EXECUTE_READWRITE, &oldProtect);
        memcpy(g_hookTarget, g_originalBytes, 5);
        VirtualProtect(g_hookTarget, 5, oldProtect, &oldProtect);
        g_hookInstalled = false;
        std::cout << "[+] Hook removed.\n";
    }
}

// ============================================================================
// HackThread — Sets up everything and handles keybinds
// ============================================================================
DWORD WINAPI HackThread(HMODULE hModule)
{
    AllocConsole();
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);

    std::cout << "========================================\n";
    std::cout << "  GameHack ESP — AC 1.3.0.2\n";
    std::cout << "========================================\n\n";

    // Get base address
    g_baseAddress = (uintptr_t)GetModuleHandleA("ac_client.exe");
    std::cout << "[*] ac_client.exe base: 0x" << std::hex << g_baseAddress << std::dec << "\n";

    if (g_baseAddress == 0)
    {
        std::cout << "[ERROR] Could not find ac_client.exe!\n";
        goto cleanup;
    }

    // Install the OpenGL hook
    if (!InstallHook())
    {
        std::cout << "[ERROR] Failed to install hook!\n";
        goto cleanup;
    }

    std::cout << "\n";
    std::cout << "========================================\n";
    std::cout << "  CONTROLS:\n";
    std::cout << "  INSERT  = Toggle ESP on/off\n";
    std::cout << "  F1      = Toggle boxes\n";
    std::cout << "  F2      = Toggle names\n";
    std::cout << "  F3      = Toggle health bars\n";
    std::cout << "  F4      = Toggle snap lines\n";
    std::cout << "  F5      = Toggle aimbot\n";
    std::cout << "  F6      = Aimbot FOV +5\n";
    std::cout << "  F7      = Aimbot FOV -5\n";
    std::cout << "  F8      = Smooth +/- (hold SHIFT)\n";
    std::cout << "  R-CLICK = Aim (hold while aimbot ON)\n";
    std::cout << "  END     = Eject DLL\n";
    std::cout << "========================================\n\n";

    // Main loop — handle keybinds
    while (true)
    {
        if (GetAsyncKeyState(VK_END) & 1)
            break;

        if (GetAsyncKeyState(VK_INSERT) & 1)
        {
            g_espEnabled = !g_espEnabled;
            std::cout << "[*] ESP: " << (g_espEnabled ? "ON" : "OFF") << "\n";
        }
        if (GetAsyncKeyState(VK_F1) & 1)
        {
            g_espBoxes = !g_espBoxes;
            std::cout << "[*] Boxes: " << (g_espBoxes ? "ON" : "OFF") << "\n";
        }
        if (GetAsyncKeyState(VK_F2) & 1)
        {
            g_espNames = !g_espNames;
            std::cout << "[*] Names: " << (g_espNames ? "ON" : "OFF") << "\n";
        }
        if (GetAsyncKeyState(VK_F3) & 1)
        {
            g_espHealth = !g_espHealth;
            std::cout << "[*] Health bars: " << (g_espHealth ? "ON" : "OFF") << "\n";
        }
        if (GetAsyncKeyState(VK_F4) & 1)
        {
            g_espSnapLines = !g_espSnapLines;
            std::cout << "[*] Snap lines: " << (g_espSnapLines ? "ON" : "OFF") << "\n";
        }
        if (GetAsyncKeyState(VK_F5) & 1)
        {
            g_aimbotEnabled = !g_aimbotEnabled;
            std::cout << "[*] Aimbot: " << (g_aimbotEnabled ? "ON" : "OFF") << "\n";
        }
        if (GetAsyncKeyState(VK_F6) & 1)
        {
            g_aimbotFOV += 5.0f;
            if (g_aimbotFOV > 90.0f) g_aimbotFOV = 90.0f;
            std::cout << "[*] Aimbot FOV: " << g_aimbotFOV << "\n";
        }
        if (GetAsyncKeyState(VK_F7) & 1)
        {
            g_aimbotFOV -= 5.0f;
            if (g_aimbotFOV < 5.0f) g_aimbotFOV = 5.0f;
            std::cout << "[*] Aimbot FOV: " << g_aimbotFOV << "\n";
        }
        if (GetAsyncKeyState(VK_F8) & 1)
        {
            // SHIFT+F8 = decrease smooth (faster), F8 alone = increase (smoother)
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
            {
                g_aimbotSmooth -= 1.0f;
                if (g_aimbotSmooth < 1.0f) g_aimbotSmooth = 1.0f;
            }
            else
            {
                g_aimbotSmooth += 1.0f;
                if (g_aimbotSmooth > 20.0f) g_aimbotSmooth = 20.0f;
            }
            std::cout << "[*] Aimbot smooth: " << g_aimbotSmooth
                      << (g_aimbotSmooth <= 1.0f ? " (INSTANT)" : "") << "\n";
        }

        Sleep(10);
    }

    // Clean up
    std::cout << "[*] Ejecting...\n";
    RemoveHook();

cleanup:
    if (f) fclose(f);
    FreeConsole();
    FreeLibraryAndExitThread(hModule, 0);
    return 0;
}

// ============================================================================
// DllMain
// ============================================================================
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)HackThread, hModule, 0, nullptr);
        break;
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}
