// editor.cpp — редактор карт на raylib. Сохраняет карту в txt (см. map.h).
#include "map.h"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include <cmath>
#include <string>
#include <vector>

static const Color PALETTE[] = {
    {220, 70, 60, 255},  {240, 170, 50, 255}, {240, 220, 70, 255}, {90, 190, 90, 255},
    {70, 150, 230, 255}, {150, 100, 220, 255}, {235, 235, 235, 255}, {110, 110, 120, 255}};
static const int PALETTE_N = 8;
static const float SNAPS[] = {1.0f, 0.5f, 0.25f, 0.1f};
static const char* AXIS_NAMES[] = {"X", "Y", "Z", "ALL"};

// ---------- Орбитальная камера ----------
struct OrbitCam {
    Vector3 target{0, 0, 0};
    float yaw = 0.8f, pitch = 0.6f, dist = 14.0f;

    Camera3D Get() const {
        Camera3D c{};
        c.position = {target.x + dist * cosf(pitch) * sinf(yaw), target.y + dist * sinf(pitch),
                      target.z + dist * cosf(pitch) * cosf(yaw)};
        c.target = target;
        c.up = {0, 1, 0};
        c.fovy = 60.0f;
        c.projection = CAMERA_PERSPECTIVE;
        return c;
    }
};

static bool RayHitGround(Ray ray, Vector3& out) {
    if (fabsf(ray.direction.y) < 1e-5f) return false;
    float t = -ray.position.y / ray.direction.y;
    if (t <= 0) return false;
    out = Vector3Add(ray.position, Vector3Scale(ray.direction, t));
    return true;
}

static float Snap(float v, float step) { return roundf(v / step) * step; }

static int PickObject(const std::vector<MapObject>& objs, Ray ray) {
    int best = -1;
    float bestDist = 1e30f;
    for (int i = 0; i < (int)objs.size(); i++) {
        RayCollision c = GetRayCollisionBox(ray, ObjectBounds(objs[i]));
        if (c.hit && c.distance < bestDist) {
            bestDist = c.distance;
            best = i;
        }
    }
    return best;
}

// Ближайшее к камере "осевое" направление для стрелок.
static Vector3 AxisDir(float x, float z) {
    if (fabsf(x) > fabsf(z)) return {x > 0 ? 1.0f : -1.0f, 0, 0};
    return {0, 0, z > 0 ? 1.0f : -1.0f};
}

static void ResizeObject(MapObject& o, int axis, float delta) {
    auto add = [&](float& v) { v = std::max(0.1f, v + delta); };
    switch (o.type) {
        case ShapeType::Cube:
            if (axis == 0 || axis == 3) add(o.size.x);
            if (axis == 1 || axis == 3) add(o.size.y);
            if (axis == 2 || axis == 3) add(o.size.z);
            break;
        case ShapeType::Sphere:  // сфера — всегда равномерно
            add(o.size.x);
            o.size.y = o.size.z = o.size.x;
            break;
        case ShapeType::Cylinder:
            if (axis == 1) {
                add(o.size.y);
            } else {
                if (axis == 3) add(o.size.y);
                add(o.size.x);
                o.size.z = o.size.x;
            }
            break;
        case ShapeType::Plane:
            if (axis == 0 || axis == 3) add(o.size.x);
            if (axis == 2 || axis == 3) add(o.size.z);
            break;
    }
}

static bool KeyRepeat(int key) { return IsKeyPressed(key) || IsKeyPressedRepeat(key); }

int main(int argc, char** argv) {
    std::string path = argc > 1 ? argv[1] : "map.txt";

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "MapForge Editor");
    SetExitKey(KEY_NULL);  // Esc больше не закрывает окно
    SetTargetFPS(60);

    std::vector<MapObject> objs;
    std::string status;
    float statusTime = 0;
    auto say = [&](const std::string& s) {
        status = s;
        statusTime = 4.0f;
    };

    if (LoadMap(path, objs)) say("Loaded " + path + " (" + std::to_string(objs.size()) + " objects)");
    else say("New map: " + path + " (Ctrl+S to save)");

    OrbitCam cam;
    int sel = -1;
    ShapeType curType = ShapeType::Cube;
    int colorIdx = 4;
    int snapIdx = 1;
    int axis = 3;
    bool renaming = false;
    std::string nameBuf;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (statusTime > 0) statusTime -= dt;
        float snap = SNAPS[snapIdx];
        bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) ||
                    IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
        bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

        // ---------- Режим ввода имени ----------
        if (renaming) {
            for (int c = GetCharPressed(); c > 0; c = GetCharPressed())
                if (c > 32 && c < 127 && nameBuf.size() < 32) nameBuf.push_back((char)c);
            if (KeyRepeat(KEY_BACKSPACE) && !nameBuf.empty()) nameBuf.pop_back();
            if (IsKeyPressed(KEY_ENTER) && sel >= 0) {
                objs[sel].name = nameBuf;
                renaming = false;
                say("Name set: " + (nameBuf.empty() ? std::string("(none)") : nameBuf));
            }
            if (IsKeyPressed(KEY_ESCAPE)) renaming = false;
        } else {
            // ---------- Камера ----------
            if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
                Vector2 d = GetMouseDelta();
                cam.yaw -= d.x * 0.005f;
                cam.pitch = Clamp(cam.pitch + d.y * 0.005f, 0.05f, 1.5f);
            }
            cam.dist = Clamp(cam.dist * (1.0f - GetMouseWheelMove() * 0.1f), 2.0f, 120.0f);

            Vector2 fwd{-sinf(cam.yaw), -cosf(cam.yaw)};
            Vector2 right{-fwd.y, fwd.x};
            float spd = cam.dist * (shift ? 1.6f : 0.8f) * dt;
            if (!ctrl) {
                if (IsKeyDown(KEY_W)) { cam.target.x += fwd.x * spd;   cam.target.z += fwd.y * spd; }
                if (IsKeyDown(KEY_S)) { cam.target.x -= fwd.x * spd;   cam.target.z -= fwd.y * spd; }
                if (IsKeyDown(KEY_D)) { cam.target.x += right.x * spd; cam.target.z += right.y * spd; }
                if (IsKeyDown(KEY_A)) { cam.target.x -= right.x * spd; cam.target.z -= right.y * spd; }
            }

            Camera3D camera = cam.Get();
            Ray ray = GetScreenToWorldRay(GetMousePosition(), camera);

            // ---------- Выбор / размещение ----------
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                Vector3 hit;
                if (shift && RayHitGround(ray, hit)) {
                    MapObject o;
                    o.type = curType;
                    o.size = (curType == ShapeType::Plane) ? Vector3{4, 0, 4} : Vector3{1, 1, 1};
                    o.color = PALETTE[colorIdx];
                    float h = curType == ShapeType::Plane ? 0.0f : 0.5f;
                    o.pos = {Snap(hit.x, snap), h, Snap(hit.z, snap)};
                    objs.push_back(o);
                    sel = (int)objs.size() - 1;
                    say(std::string("Placed ") + ShapeName(curType));
                } else {
                    sel = PickObject(objs, ray);
                }
            }

            // ---------- Выбор типа / цвета / сетки ----------
            if (IsKeyPressed(KEY_ONE))   curType = ShapeType::Cube;
            if (IsKeyPressed(KEY_TWO))   curType = ShapeType::Sphere;
            if (IsKeyPressed(KEY_THREE)) curType = ShapeType::Cylinder;
            if (IsKeyPressed(KEY_FOUR))  curType = ShapeType::Plane;
            if (IsKeyPressed(KEY_G)) snapIdx = (snapIdx + 1) % 4;
            if (IsKeyPressed(KEY_TAB)) axis = (axis + 1) % 4;
            if (IsKeyPressed(KEY_C) && !ctrl) {
                colorIdx = (colorIdx + 1) % PALETTE_N;
                if (sel >= 0) objs[sel].color = PALETTE[colorIdx];
            }

            // ---------- Редактирование выбранного ----------
            if (sel >= 0) {
                MapObject& o = objs[sel];
                Vector3 f = AxisDir(fwd.x, fwd.y);
                Vector3 r = AxisDir(right.x, right.y);
                if (KeyRepeat(KEY_UP))    o.pos = Vector3Add(o.pos, Vector3Scale(f, snap));
                if (KeyRepeat(KEY_DOWN))  o.pos = Vector3Subtract(o.pos, Vector3Scale(f, snap));
                if (KeyRepeat(KEY_RIGHT)) o.pos = Vector3Add(o.pos, Vector3Scale(r, snap));
                if (KeyRepeat(KEY_LEFT))  o.pos = Vector3Subtract(o.pos, Vector3Scale(r, snap));
                if (KeyRepeat(KEY_R)) o.pos.y += snap;
                if (KeyRepeat(KEY_F)) o.pos.y -= snap;
                if (KeyRepeat(KEY_EQUAL) || KeyRepeat(KEY_KP_ADD))      ResizeObject(o, axis, snap);
                if (KeyRepeat(KEY_MINUS) || KeyRepeat(KEY_KP_SUBTRACT)) ResizeObject(o, axis, -snap);

                if (IsKeyPressed(KEY_N)) {
                    renaming = true;
                    nameBuf = o.name;
                    while (GetCharPressed() > 0) {}  // выбросить 'n', которой мы вошли в режим
                }
                if (ctrl && IsKeyPressed(KEY_D)) {
                    MapObject copy = o;
                    copy.pos.x += 1.0f;
                    copy.name.clear();  // имена должны быть уникальными
                    objs.push_back(copy);
                    sel = (int)objs.size() - 1;
                    say("Duplicated");
                }
                if (IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE)) {
                    objs.erase(objs.begin() + sel);
                    sel = -1;
                    say("Deleted");
                }
            }
            if (IsKeyPressed(KEY_ESCAPE)) sel = -1;

            // ---------- Файл ----------
            if (ctrl && IsKeyPressed(KEY_S)) say(SaveMap(path, objs) ? "Saved " + path : "ERROR: cannot write " + path);
            if (ctrl && IsKeyPressed(KEY_O)) {
                if (LoadMap(path, objs)) {
                    sel = -1;
                    say("Loaded " + path);
                } else say("ERROR: cannot read " + path);
            }
        }

        // ---------- Рисование ----------
        Camera3D camera = cam.Get();
        BeginDrawing();
        ClearBackground({38, 42, 52, 255});

        BeginMode3D(camera);
        for (int i = 0; i < (int)objs.size(); i++) {
            const MapObject& o = objs[i];
            Color wire = (i == sel) ? YELLOW : Fade(BLACK, 0.55f);
            switch (o.type) {
                case ShapeType::Cube:
                    DrawCubeV(o.pos, o.size, o.color);
                    DrawCubeWiresV(o.pos, o.size, wire);
                    break;
                case ShapeType::Sphere: {
                    float rad = o.size.x * 0.5f;
                    DrawSphere(o.pos, rad, o.color);
                    DrawSphereWires(o.pos, rad, 8, 8, (i == sel) ? YELLOW : Fade(BLACK, 0.25f));
                    break;
                }
                case ShapeType::Cylinder: {
                    float rad = o.size.x * 0.5f;
                    Vector3 base{o.pos.x, o.pos.y - o.size.y * 0.5f, o.pos.z};
                    DrawCylinder(base, rad, rad, o.size.y, 24, o.color);
                    DrawCylinderWires(base, rad, rad, o.size.y, 24, wire);
                    break;
                }
                case ShapeType::Plane:
                    DrawPlane(o.pos, {o.size.x, o.size.z}, o.color);
                    break;
            }
            if (i == sel) DrawBoundingBox(ObjectBounds(o), YELLOW);
        }

        rlPushMatrix();
        rlTranslatef(0, 0.003f, 0);  // чуть выше y=0, чтобы сетка не мерцала на плоскостях
        DrawGrid(80, 1.0f);
        rlPopMatrix();
        DrawLine3D({0, 0.01f, 0}, {3, 0.01f, 0}, RED);
        DrawLine3D({0, 0.01f, 0}, {0, 3, 0}, GREEN);
        DrawLine3D({0, 0.01f, 0}, {0, 0.01f, 3}, BLUE);

        if (shift && !renaming) {  // призрак будущей фигуры
            Vector3 hit;
            if (RayHitGround(GetScreenToWorldRay(GetMousePosition(), camera), hit)) {
                Vector3 p{Snap(hit.x, snap), 0.5f, Snap(hit.z, snap)};
                DrawCubeWiresV(p, {1, 1, 1}, Fade(PALETTE[colorIdx], 0.9f));
            }
        }
        EndMode3D();

        // ---------- HUD ----------
        const char* help =
            "RMB drag: orbit   Wheel: zoom   WASD: move camera (Shift = fast)\n"
            "LMB: select   Shift+LMB: place shape   1-4: cube/sphere/cylinder/plane\n"
            "Arrows: move (X/Z)   R/F: up/down   Tab: scale axis   +/-: scale\n"
            "C: color   N: rename   G: grid step   Ctrl+D: duplicate   Del: delete\n"
            "Ctrl+S: save   Ctrl+O: reload   Esc: deselect";
        DrawRectangle(0, 0, 640, 108, Fade(BLACK, 0.55f));
        DrawText(help, 10, 8, 16, RAYWHITE);

        DrawText(TextFormat("Place: %s   Color: ", ShapeName(curType)), 10, GetScreenHeight() - 56, 18, RAYWHITE);
        DrawRectangle(10 + MeasureText(TextFormat("Place: %s   Color: ", ShapeName(curType)), 18), GetScreenHeight() - 56, 18, 18, PALETTE[colorIdx]);
        DrawText(TextFormat("Grid: %.2f   Scale axis: %s   Objects: %d", snap, AXIS_NAMES[axis], (int)objs.size()),
                 10, GetScreenHeight() - 30, 18, RAYWHITE);

        if (sel >= 0) {
            const MapObject& o = objs[sel];
            DrawRectangle(GetScreenWidth() - 330, 0, 330, 92, Fade(BLACK, 0.55f));
            DrawText(TextFormat("%s  \"%s\"", ShapeName(o.type), o.name.empty() ? "-" : o.name.c_str()),
                     GetScreenWidth() - 320, 8, 18, YELLOW);
            DrawText(TextFormat("pos  %.2f %.2f %.2f", o.pos.x, o.pos.y, o.pos.z), GetScreenWidth() - 320, 34, 18, RAYWHITE);
            DrawText(TextFormat("size %.2f %.2f %.2f", o.size.x, o.size.y, o.size.z), GetScreenWidth() - 320, 58, 18, RAYWHITE);
        }
        if (renaming) {
            int w = GetScreenWidth(), h = GetScreenHeight();
            DrawRectangle(w / 2 - 200, h / 2 - 40, 400, 80, Fade(BLACK, 0.85f));
            DrawText("Object name (Enter = OK, Esc = cancel):", w / 2 - 190, h / 2 - 32, 16, RAYWHITE);
            DrawText((nameBuf + "_").c_str(), w / 2 - 190, h / 2 - 4, 28, YELLOW);
        }
        if (statusTime > 0)
            DrawText(status.c_str(), GetScreenWidth() / 2 - MeasureText(status.c_str(), 20) / 2, GetScreenHeight() - 30, 20, LIME);

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
