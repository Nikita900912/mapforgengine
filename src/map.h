// map.h — общий формат карты для редактора и движка.
//
// Формат txt (одна фигура на строку, строки с '#' — комментарии):
//   тип  x y z  sx sy sz  r g b  [имя]
//
//   тип  : cube | sphere | cylinder | plane
//   x y z: центр фигуры в мире
//   размеры:
//     cube     — sx, sy, sz (ширина, высота, глубина)
//     sphere   — sx = диаметр
//     cylinder — sx = диаметр, sy = высота
//     plane    — sx, sz (плоскость лежит на высоте y), sy не используется
//   r g b: цвет 0..255
//   имя  : необязательно, без пробелов — по нему Lua находит объект
#pragma once
#include "raylib.h"
#include <algorithm>
#include <fstream>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

enum class ShapeType { Cube = 0, Sphere = 1, Cylinder = 2, Plane = 3 };

inline const char* ShapeName(ShapeType t) {
    switch (t) {
        case ShapeType::Cube:     return "cube";
        case ShapeType::Sphere:   return "sphere";
        case ShapeType::Cylinder: return "cylinder";
        case ShapeType::Plane:    return "plane";
    }
    return "cube";
}

inline bool ShapeFromName(const std::string& s, ShapeType& out) {
    if (s == "cube")     { out = ShapeType::Cube;     return true; }
    if (s == "sphere")   { out = ShapeType::Sphere;   return true; }
    if (s == "cylinder") { out = ShapeType::Cylinder; return true; }
    if (s == "plane")    { out = ShapeType::Plane;    return true; }
    return false;
}

struct MapObject {
    ShapeType type = ShapeType::Cube;
    Vector3 pos{0, 0, 0};
    Vector3 size{1, 1, 1};
    Color color{200, 200, 200, 255};
    std::string name;  // пусто = без имени
};

inline bool SaveMap(const std::string& path, const std::vector<MapObject>& objs) {
    std::ofstream f(path);
    if (!f) return false;
    f.imbue(std::locale::classic());
    f << "# MapForge map v1\n# type x y z sx sy sz r g b [name]\n";
    for (const MapObject& o : objs) {
        f << ShapeName(o.type) << ' '
          << o.pos.x << ' ' << o.pos.y << ' ' << o.pos.z << ' '
          << o.size.x << ' ' << o.size.y << ' ' << o.size.z << ' '
          << (int)o.color.r << ' ' << (int)o.color.g << ' ' << (int)o.color.b;
        if (!o.name.empty()) f << ' ' << o.name;
        f << '\n';
    }
    return (bool)f;
}

// Возвращает false, только если файл не удалось открыть. Битые строки пропускаются.
inline bool LoadMap(const std::string& path, std::vector<MapObject>& out) {
    std::ifstream f(path);
    if (!f) return false;
    out.clear();
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#') continue;

        std::istringstream ss(line);
        ss.imbue(std::locale::classic());
        std::string type;
        MapObject o;
        int r, g, b;
        ss >> type;
        if (!ShapeFromName(type, o.type)) continue;
        if (!(ss >> o.pos.x >> o.pos.y >> o.pos.z >> o.size.x >> o.size.y >> o.size.z >> r >> g >> b)) continue;
        o.color = Color{(unsigned char)std::clamp(r, 0, 255), (unsigned char)std::clamp(g, 0, 255),
                        (unsigned char)std::clamp(b, 0, 255), 255};
        ss >> o.name;
        out.push_back(o);
    }
    return true;
}

// Ограничивающий параллелепипед (для выбора мышью и столкновений в Lua).
inline BoundingBox ObjectBounds(const MapObject& o) {
    Vector3 h{};
    switch (o.type) {
        case ShapeType::Cube:     h = {o.size.x * 0.5f, o.size.y * 0.5f, o.size.z * 0.5f}; break;
        case ShapeType::Sphere:   h = {o.size.x * 0.5f, o.size.x * 0.5f, o.size.x * 0.5f}; break;
        case ShapeType::Cylinder: h = {o.size.x * 0.5f, o.size.y * 0.5f, o.size.x * 0.5f}; break;
        case ShapeType::Plane:    h = {o.size.x * 0.5f, 0.05f, o.size.z * 0.5f}; break;
    }
    return BoundingBox{{o.pos.x - h.x, o.pos.y - h.y, o.pos.z - h.z},
                       {o.pos.x + h.x, o.pos.y + h.y, o.pos.z + h.z}};
}
