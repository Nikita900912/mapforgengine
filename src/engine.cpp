// engine.cpp — движок: загружает карту из txt, рисует с освещением, игровая логика — на Lua.
#include "map.h"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include <cctype>
#include <cstdio>
#include <map>
#include <string>
#include <unordered_map>

// ======================= Состояние движка =======================
struct Engine {
    lua_State* L = nullptr;
    std::map<int, MapObject> objects;  // id -> объект (id стабильны, пока объект жив)
    int nextId = 1;
    Camera3D cam{};
    Color skyTop{70, 130, 200, 255}, skyBottom{200, 225, 245, 255};
    std::string mapPath, scriptPath;
    std::string error;
    bool scriptOk = false;
    bool mouseLocked = false;
    int skipMouseFrames = 0;  // после захвата курсора первые дельты мыши — мусор
    bool quit = false;
    Vector3 lightDir{-0.5f, -1.0f, -0.35f};
} G;

// ======================= Рендер (освещённые меши) =======================
static const char* VS = R"(#version 330
in vec3 vertexPosition;
in vec3 vertexNormal;
uniform mat4 mvp;
uniform mat4 matModel;
out vec3 fragNormal;
void main() {
    fragNormal = normalize(mat3(transpose(inverse(matModel))) * vertexNormal);
    gl_Position = mvp * vec4(vertexPosition, 1.0);
})";

static const char* FS = R"(#version 330
in vec3 fragNormal;
uniform vec4 colDiffuse;
uniform vec3 lightDir;
out vec4 finalColor;
void main() {
    vec3 n = normalize(fragNormal);
    float diff = max(dot(n, -normalize(lightDir)), 0.0);
    vec3 amb = mix(vec3(0.22, 0.20, 0.18), vec3(0.42, 0.48, 0.58), 0.5 + 0.5 * n.y);
    vec3 c = colDiffuse.rgb * (amb + diff * vec3(1.0, 0.95, 0.85) * 0.8);
    finalColor = vec4(min(c, vec3(1.0)), colDiffuse.a);
})";

static Shader gShader;
static Model gModels[4];
static Vector3 gCenter[4];  // смещение центра меша, чтобы pos всегда был центром фигуры

static void UpdateLight() {
    Vector3 d = Vector3Normalize(G.lightDir);
    SetShaderValue(gShader, GetShaderLocation(gShader, "lightDir"), &d, SHADER_UNIFORM_VEC3);
}

static void InitRender() {
    gShader = LoadShaderFromMemory(VS, FS);
    Mesh meshes[4] = {GenMeshCube(1, 1, 1), GenMeshSphere(0.5f, 24, 32), GenMeshCylinder(0.5f, 1.0f, 32),
                      GenMeshPlane(1, 1, 1, 1)};
    for (int i = 0; i < 4; i++) {
        BoundingBox bb = GetMeshBoundingBox(meshes[i]);
        gCenter[i] = {(bb.min.x + bb.max.x) * 0.5f, (bb.min.y + bb.max.y) * 0.5f, (bb.min.z + bb.max.z) * 0.5f};
        gModels[i] = LoadModelFromMesh(meshes[i]);
        gModels[i].materials[0].shader = gShader;
    }
    UpdateLight();
}

static void ShutdownRender() {
    for (int i = 0; i < 4; i++) UnloadModel(gModels[i]);
    UnloadShader(gShader);
}

static void DrawObject(const MapObject& o) {
    int i = (int)o.type;
    Vector3 sc{};
    switch (o.type) {
        case ShapeType::Cube:     sc = o.size; break;
        case ShapeType::Sphere:   sc = {o.size.x, o.size.x, o.size.x}; break;
        case ShapeType::Cylinder: sc = {o.size.x, o.size.y, o.size.x}; break;
        case ShapeType::Plane:    sc = {o.size.x, 1.0f, o.size.z}; break;
    }
    Vector3 p{o.pos.x - gCenter[i].x * sc.x, o.pos.y - gCenter[i].y * sc.y, o.pos.z - gCenter[i].z * sc.z};
    DrawModelEx(gModels[i], p, {0, 1, 0}, 0.0f, sc, o.color);
}

// ======================= Lua API =======================
static MapObject& GetObj(lua_State* L, int idx) {
    int id = (int)luaL_checkinteger(L, idx);
    auto it = G.objects.find(id);
    if (it == G.objects.end()) luaL_error(L, "object with id %d does not exist (was it removed?)", id);
    return it->second;
}

static unsigned char ToByte(lua_Number v) { return (unsigned char)Clamp((float)v, 0.0f, 255.0f); }

static int KeyFromName(const char* s) {
    std::string n;
    for (const char* p = s; *p; p++) n.push_back((char)toupper((unsigned char)*p));
    if (n.size() == 1 && ((n[0] >= 'A' && n[0] <= 'Z') || (n[0] >= '0' && n[0] <= '9'))) return n[0];
    static const std::unordered_map<std::string, int> keys = {
        {"SPACE", KEY_SPACE}, {"ENTER", KEY_ENTER}, {"ESC", KEY_ESCAPE}, {"TAB", KEY_TAB},
        {"UP", KEY_UP},       {"DOWN", KEY_DOWN},   {"LEFT", KEY_LEFT},  {"RIGHT", KEY_RIGHT},
        {"SHIFT", KEY_LEFT_SHIFT}, {"CTRL", KEY_LEFT_CONTROL}, {"ALT", KEY_LEFT_ALT},
        {"F1", KEY_F1}, {"F2", KEY_F2}, {"F3", KEY_F3}, {"F4", KEY_F4}};
    auto it = keys.find(n);
    return it == keys.end() ? KEY_NULL : it->second;
}

static int l_find(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    for (auto& kv : G.objects) {
        if (kv.second.name == name) {
            lua_pushinteger(L, kv.first);
            return 1;
        }
    }
    lua_pushnil(L);
    return 1;
}

static int l_objects(lua_State* L) {
    lua_createtable(L, (int)G.objects.size(), 0);
    int i = 1;
    for (auto& kv : G.objects) {
        lua_pushinteger(L, kv.first);
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

static int l_exists(lua_State* L) {
    lua_pushboolean(L, G.objects.count((int)luaL_checkinteger(L, 1)) > 0);
    return 1;
}

static int l_get_pos(lua_State* L) {
    MapObject& o = GetObj(L, 1);
    lua_pushnumber(L, o.pos.x); lua_pushnumber(L, o.pos.y); lua_pushnumber(L, o.pos.z);
    return 3;
}
static int l_set_pos(lua_State* L) {
    MapObject& o = GetObj(L, 1);
    o.pos = {(float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4)};
    return 0;
}
static int l_get_size(lua_State* L) {
    MapObject& o = GetObj(L, 1);
    lua_pushnumber(L, o.size.x); lua_pushnumber(L, o.size.y); lua_pushnumber(L, o.size.z);
    return 3;
}
static int l_set_size(lua_State* L) {
    MapObject& o = GetObj(L, 1);
    o.size = {(float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4)};
    return 0;
}
static int l_get_color(lua_State* L) {
    MapObject& o = GetObj(L, 1);
    lua_pushinteger(L, o.color.r); lua_pushinteger(L, o.color.g); lua_pushinteger(L, o.color.b);
    return 3;
}
static int l_set_color(lua_State* L) {
    MapObject& o = GetObj(L, 1);
    o.color = {ToByte(luaL_checknumber(L, 2)), ToByte(luaL_checknumber(L, 3)), ToByte(luaL_checknumber(L, 4)), 255};
    return 0;
}
static int l_get_type(lua_State* L) {
    lua_pushstring(L, ShapeName(GetObj(L, 1).type));
    return 1;
}
static int l_get_name(lua_State* L) {
    lua_pushstring(L, GetObj(L, 1).name.c_str());
    return 1;
}
static int l_remove(lua_State* L) {
    G.objects.erase((int)luaL_checkinteger(L, 1));
    return 0;
}

// spawn(type, x,y,z, sx,sy,sz, r,g,b, name) -> id
static int l_spawn(lua_State* L) {
    const char* typeStr = luaL_checkstring(L, 1);
    ShapeType t;
    if (!ShapeFromName(typeStr, t)) return luaL_error(L, "unknown shape '%s' (cube/sphere/cylinder/plane)", typeStr);
    float v[6];
    for (int i = 0; i < 3; i++) v[i] = (float)luaL_optnumber(L, 2 + i, 0.0);
    for (int i = 3; i < 6; i++) v[i] = (float)luaL_optnumber(L, 2 + i, 1.0);
    unsigned char c[3];
    for (int i = 0; i < 3; i++) c[i] = ToByte(luaL_optnumber(L, 8 + i, 200.0));
    const char* name = luaL_optstring(L, 11, "");

    MapObject o;
    o.type = t;
    o.pos = {v[0], v[1], v[2]};
    o.size = {v[3], v[4], v[5]};
    o.color = {c[0], c[1], c[2], 255};
    o.name = name;
    int id = G.nextId++;
    G.objects[id] = o;
    lua_pushinteger(L, id);
    return 1;
}

static int l_overlaps(lua_State* L) {
    MapObject& a = GetObj(L, 1);
    BoundingBox ba = ObjectBounds(a);
    MapObject& b = GetObj(L, 2);
    lua_pushboolean(L, CheckCollisionBoxes(ba, ObjectBounds(b)));
    return 1;
}

static int l_key_down(lua_State* L) {
    const char* n = luaL_checkstring(L, 1);
    int k = KeyFromName(n);
    bool d = IsKeyDown(k);
    if (k == KEY_LEFT_SHIFT) d = d || IsKeyDown(KEY_RIGHT_SHIFT);
    if (k == KEY_LEFT_CONTROL) d = d || IsKeyDown(KEY_RIGHT_CONTROL);
    lua_pushboolean(L, k != KEY_NULL && d);
    return 1;
}
static int l_key_pressed(lua_State* L) {
    int k = KeyFromName(luaL_checkstring(L, 1));
    lua_pushboolean(L, k != KEY_NULL && IsKeyPressed(k));
    return 1;
}
// mouse_down(1|2|3) — левая, правая, средняя
static int l_mouse_down(lua_State* L) {
    lua_pushboolean(L, IsMouseButtonDown((int)luaL_optinteger(L, 1, 1) - 1));
    return 1;
}
static int l_mouse_pressed(lua_State* L) {
    lua_pushboolean(L, IsMouseButtonPressed((int)luaL_optinteger(L, 1, 1) - 1));
    return 1;
}
static int l_mouse_delta(lua_State* L) {
    Vector2 d = G.skipMouseFrames > 0 ? Vector2{0, 0} : GetMouseDelta();
    lua_pushnumber(L, d.x); lua_pushnumber(L, d.y);
    return 2;
}
static int l_lock_mouse(lua_State* L) {
    bool lock = lua_toboolean(L, 1) != 0;
    if (lock && !G.mouseLocked) {
        DisableCursor();
        G.skipMouseFrames = 3;
    }
    if (!lock && G.mouseLocked) EnableCursor();
    G.mouseLocked = lock;
    return 0;
}

// camera(px,py,pz, tx,ty,tz [, fov])
static int l_camera(lua_State* L) {
    G.cam.position = {(float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3)};
    G.cam.target = {(float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5), (float)luaL_checknumber(L, 6)};
    G.cam.fovy = (float)luaL_optnumber(L, 7, G.cam.fovy);
    return 0;
}
// sky(r1,g1,b1, r2,g2,b2) — градиент неба: верх и горизонт
static int l_sky(lua_State* L) {
    G.skyTop = {ToByte(luaL_checknumber(L, 1)), ToByte(luaL_checknumber(L, 2)), ToByte(luaL_checknumber(L, 3)), 255};
    G.skyBottom = {ToByte(luaL_checknumber(L, 4)), ToByte(luaL_checknumber(L, 5)), ToByte(luaL_checknumber(L, 6)), 255};
    return 0;
}
static int l_light(lua_State* L) {
    G.lightDir = {(float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3)};
    UpdateLight();
    return 0;
}
// draw_text(text, x, y [, size, r, g, b]) — вызывать из on_draw2d
static int l_draw_text(lua_State* L) {
    const char* text = luaL_checkstring(L, 1);
    int x = (int)luaL_checknumber(L, 2), y = (int)luaL_checknumber(L, 3);
    int size = (int)luaL_optinteger(L, 4, 20);
    Color c{ToByte(luaL_optnumber(L, 5, 255)), ToByte(luaL_optnumber(L, 6, 255)), ToByte(luaL_optnumber(L, 7, 255)), 255};
    DrawText(text, x, y, size, c);
    return 0;
}
static int l_screen_size(lua_State* L) {
    lua_pushinteger(L, GetScreenWidth()); lua_pushinteger(L, GetScreenHeight());
    return 2;
}
static int l_time(lua_State* L) {
    lua_pushnumber(L, GetTime());
    return 1;
}
// screenshot(path) — сохранить текущий кадр в png (вызывать из on_draw2d)
static int l_screenshot(lua_State* L) {
    rlDrawRenderBatchActive();  // дорисовать накопленный 2D-текст
    TakeScreenshot(luaL_checkstring(L, 1));
    return 0;
}
static int l_quit(lua_State*) {
    G.quit = true;
    return 0;
}

static const luaL_Reg API[] = {
    {"find", l_find}, {"objects", l_objects}, {"exists", l_exists},
    {"get_pos", l_get_pos}, {"set_pos", l_set_pos}, {"get_size", l_get_size}, {"set_size", l_set_size},
    {"get_color", l_get_color}, {"set_color", l_set_color}, {"get_type", l_get_type}, {"get_name", l_get_name},
    {"spawn", l_spawn}, {"remove", l_remove}, {"overlaps", l_overlaps},
    {"key_down", l_key_down}, {"key_pressed", l_key_pressed},
    {"mouse_down", l_mouse_down}, {"mouse_pressed", l_mouse_pressed}, {"mouse_delta", l_mouse_delta},
    {"lock_mouse", l_lock_mouse},
    {"camera", l_camera}, {"sky", l_sky}, {"light", l_light},
    {"draw_text", l_draw_text}, {"screen_size", l_screen_size}, {"time", l_time}, {"screenshot", l_screenshot}, {"quit", l_quit},
    {nullptr, nullptr}};

// ======================= Загрузка / запуск скрипта =======================
static int Traceback(lua_State* L) {
    const char* msg = lua_tostring(L, 1);
    luaL_traceback(L, L, msg ? msg : "(non-string error)", 1);
    return 1;
}

static void SetError(const std::string& msg) {
    G.error = msg;
    G.scriptOk = false;
    fprintf(stderr, "[lua] %s\n", msg.c_str());
}

// Вызывает глобальную функцию Lua, если она определена.
static void CallLua(const char* fn, bool withDt = false, float dt = 0.0f) {
    lua_State* L = G.L;
    lua_pushcfunction(L, Traceback);
    int handler = lua_gettop(L);
    lua_getglobal(L, fn);
    if (!lua_isfunction(L, -1)) {
        lua_settop(L, handler - 1);
        return;
    }
    int nargs = 0;
    if (withDt) {
        lua_pushnumber(L, dt);
        nargs = 1;
    }
    if (lua_pcall(L, nargs, 0, handler) != LUA_OK) SetError(lua_tostring(L, -1));
    lua_settop(L, handler - 1);
}

static void LoadWorld() {
    if (G.mouseLocked) {
        EnableCursor();
        G.mouseLocked = false;
    }
    G.error.clear();
    G.scriptOk = false;
    G.objects.clear();
    G.nextId = 1;
    G.cam.position = {10, 10, 10};
    G.cam.target = {0, 0, 0};
    G.cam.up = {0, 1, 0};
    G.cam.fovy = 60.0f;
    G.cam.projection = CAMERA_PERSPECTIVE;

    std::vector<MapObject> list;
    if (!LoadMap(G.mapPath, list)) {
        SetError("Cannot open map file: " + G.mapPath);
        return;
    }
    for (auto& o : list) G.objects[G.nextId++] = o;

    if (G.L) lua_close(G.L);
    G.L = luaL_newstate();
    luaL_openlibs(G.L);
    lua_pushglobaltable(G.L);
    luaL_setfuncs(G.L, API, 0);
    lua_pop(G.L, 1);

    // require("module") будет искать модули рядом со скриптом
    size_t slash = G.scriptPath.find_last_of("/\\");
    std::string dir = slash == std::string::npos ? "" : G.scriptPath.substr(0, slash + 1);
    lua_getglobal(G.L, "package");
    lua_getfield(G.L, -1, "path");
    std::string newPath = dir + "?.lua;" + lua_tostring(G.L, -1);
    lua_pop(G.L, 1);
    lua_pushstring(G.L, newPath.c_str());
    lua_setfield(G.L, -2, "path");
    lua_pop(G.L, 1);

    if (luaL_loadfile(G.L, G.scriptPath.c_str()) != LUA_OK) {
        SetError(lua_tostring(G.L, -1));
        return;
    }
    lua_pushcfunction(G.L, Traceback);
    lua_insert(G.L, -2);
    if (lua_pcall(G.L, 0, 0, lua_gettop(G.L) - 1) != LUA_OK) {
        SetError(lua_tostring(G.L, -1));
        return;
    }
    lua_settop(G.L, 0);
    G.scriptOk = true;
    CallLua("on_start");
}

// ======================= main =======================
int main(int argc, char** argv) {
    G.mapPath = argc > 1 ? argv[1] : "map.txt";
    G.scriptPath = argc > 2 ? argv[2] : "game.lua";

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "MapForge Engine");
    SetTargetFPS(60);
    InitRender();
    LoadWorld();

    while (!WindowShouldClose() && !G.quit) {
        float dt = GetFrameTime();
        if (G.skipMouseFrames > 0) G.skipMouseFrames--;
        if (IsKeyPressed(KEY_F5)) LoadWorld();  // горячая перезагрузка карты и скрипта
        if (G.scriptOk) CallLua("on_update", true, dt);

        BeginDrawing();
        ClearBackground(BLACK);  // обязательно: очищает и буфер глубины
        DrawRectangleGradientV(0, 0, GetScreenWidth(), GetScreenHeight(), G.skyTop, G.skyBottom);
        BeginMode3D(G.cam);
        for (auto& kv : G.objects) DrawObject(kv.second);
        EndMode3D();

        if (G.scriptOk) CallLua("on_draw2d");

        DrawFPS(GetScreenWidth() - 90, 8);
        if (!G.error.empty()) {
            std::string msg = G.error.substr(0, 700);
            int h = 40 + 18 * (int)(1 + std::count(msg.begin(), msg.end(), '\n'));
            DrawRectangle(0, GetScreenHeight() - h, GetScreenWidth(), h, Fade(MAROON, 0.9f));
            DrawText((msg + "\n[F5 = reload]").c_str(), 10, GetScreenHeight() - h + 8, 16, WHITE);
        }
        EndDrawing();
    }

    if (G.L) lua_close(G.L);
    ShutdownRender();
    CloseWindow();
    return 0;
}
