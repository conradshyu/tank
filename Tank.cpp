#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <shlobj.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace {
    constexpr int Tile = 24, Grid = 26, Field = Tile * Grid;
    constexpr int Width = 864, Height = 720, MapX = 24, MapY = 64;
    constexpr float TankSize = 20.0f, Step = 1.0f / 120.0f;
    constexpr int EnemiesPerRound = 20;
    enum class Direction { Up, Right, Down, Left };
    enum class Mode { Title, Playing, Intermission, GameOver };
    enum class Effect { Fire, Brick, Explosion, Round, GameOver, Count };
    struct Box { float x, y, w, h; };
    bool Overlaps(Box a, Box b) {
        return a.x < b.x + b.w && a.x + a.w > b.x &&
            a.y < b.y + b.h && a.y + a.h > b.y;
    }
    std::array<float, 2> Vector(Direction d) {
        switch (d) {
        case Direction::Up: return { 0, -1 };
        case Direction::Right: return { 1, 0 };
        case Direction::Down: return { 0, 1 };
        case Direction::Left: return { -1, 0 };
        }
        return { 0, 0 };
    }
    struct Tank {
        float x = 0, y = 0;
        Direction direction = Direction::Up;
        float fireTimer = 0, turnTimer = 0;
        int id = 0;
        bool alive = true;
        Box Bounds() const { return { x - TankSize / 2, y - TankSize / 2, TankSize, TankSize }; }
    };
    struct Bullet {
        float x, y;
        Direction direction;
        bool friendly, alive = true;
        int owner;
        Box Bounds() const {
            const bool vertical = direction == Direction::Up || direction == Direction::Down;
            return { x - (vertical ? 2.0f : 4.0f), y - (vertical ? 4.0f : 2.0f),
                    vertical ? 4.0f : 8.0f, vertical ? 8.0f : 4.0f };
        }
    };
    struct Blast { float x, y, time; };
    struct Input {
        bool moving = false, fire = false;
        Direction direction = Direction::Up;
    };

    class Game {
    public:
        Mode mode = Mode::Title;
        std::array<int, Grid* Grid> walls{};
        Tank player;
        std::vector<Tank> enemies;
        std::vector<Bullet> bullets;
        std::vector<Blast> blasts;
        std::vector<Effect> effects;
        int score = 0, round = 1, lives = 3, spawned = 0, destroyed = 0;
        float shield = 0, respawn = 0, spawnTimer = 0, transition = 0;
        bool baseAlive = true;
        explicit Game(unsigned seed) : random(seed) { BuildMap(); }
        static Box Base() { return { 12.0f * Tile, 24.0f * Tile, 2.0f * Tile, 2.0f * Tile }; }
        int Random(int maximum) { return static_cast<int>(random() % static_cast<unsigned>(maximum)); }
        void Start() {
            score = 0; round = 1; lives = 3;
            BeginRound();
        }
        void BuildMap() {
            walls.fill(0);
            for (int row = 3; row < 22; ++row) {
                for (int col = 2; col < 24; ++col) {
                    if ((row % 6 < 4) && (col % 4 < 2))
                        walls[static_cast<size_t>(row * Grid + col)] = 2;
                }
            }
            for (int row = 23; row < Grid; ++row)
                for (int col = 11; col <= 14; ++col)
                    if (row == 23 || col == 11 || col == 14)
                        walls[static_cast<size_t>(row * Grid + col)] = 2;
        }
        void BeginRound() {
            BuildMap();
            enemies.clear(); bullets.clear(); blasts.clear();
            spawned = 0; destroyed = 0; spawnTimer = 0.8f;
            baseAlive = true; respawn = 0; shield = 2.5f;
            player = Tank{ 10.5f * Tile, 24.5f * Tile, Direction::Up, 0, 0, 0, true };
            mode = Mode::Playing;
        }
        bool WallAt(Box box) const {
            if (box.x < 0 || box.y < 0 || box.x + box.w > Field || box.y + box.h > Field)
                return true;
            const int left = static_cast<int>(box.x) / Tile;
            const int top = static_cast<int>(box.y) / Tile;
            const int right = static_cast<int>(box.x + box.w - 0.01f) / Tile;
            const int bottom = static_cast<int>(box.y + box.h - 0.01f) / Tile;
            for (int y = top; y <= bottom; ++y)
                for (int x = left; x <= right; ++x)
                    if (walls[static_cast<size_t>(y * Grid + x)] != 0)
                        return true;
            return Overlaps(box, Base());
        }
        bool CanOccupy(Box box, int id) const {
            if (WallAt(box)) return false;
            if (id != 0 && player.alive && Overlaps(box, player.Bounds())) return false;
            for (const Tank& enemy : enemies)
                if (enemy.alive && enemy.id != id && Overlaps(box, enemy.Bounds())) return false;
            return true;
        }
        bool Move(Tank& tank, Direction direction, float distance) {
            Tank next = tank;
            // Small perpendicular snaps let a tank turn cleanly into tile-width corridors.
            if (direction == Direction::Up || direction == Direction::Down) {
                const float center = std::round((next.x - Tile / 2.0f) / Tile) * Tile + Tile / 2.0f;
                if (std::abs(center - next.x) <= 4.0f) next.x = center;
            }
            else {
                const float center = std::round((next.y - Tile / 2.0f) / Tile) * Tile + Tile / 2.0f;
                if (std::abs(center - next.y) <= 4.0f) next.y = center;
            }
            tank.direction = direction;
            const auto delta = Vector(direction);
            next.x += delta[0] * distance; next.y += delta[1] * distance;
            if (!CanOccupy(next.Bounds(), tank.id)) return false;
            tank.x = next.x; tank.y = next.y;
            return true;
        }
        void Fire(Tank& tank, bool friendly) {
            if (!tank.alive || tank.fireTimer > 0) return;
            for (const Bullet& bullet : bullets)
                if (bullet.alive && bullet.owner == tank.id) return;
            const auto delta = Vector(tank.direction);
            bullets.push_back({ tank.x + delta[0] * 12, tank.y + delta[1] * 12,
                               tank.direction, friendly, true, tank.id });
            tank.fireTimer = friendly ? 0.24f : 1.1f + Random(100) / 100.0f;
            if (friendly) effects.push_back(Effect::Fire);
        }
        void Lose() {
            if (mode != Mode::Playing) return;
            mode = Mode::GameOver;
            effects.push_back(Effect::GameOver);
        }
        void SpawnEnemy() {
            const std::array<int, 3> columns{ 1, 13, 24 };
            const int first = Random(3);
            for (int offset = 0; offset < 3; ++offset) {
                const int col = columns[static_cast<size_t>((first + offset) % 3)];
                Tank enemy{ (col + 0.5f) * Tile, 1.5f * Tile, Direction::Down,
                           0.8f, 0.5f, spawned + 1, true };
                if (CanOccupy(enemy.Bounds(), enemy.id)) {
                    enemies.push_back(enemy);
                    ++spawned;
                    return;
                }
            }
        }
        void UpdateBullets(float dt) {
            for (Bullet& bullet : bullets) {
                if (!bullet.alive) continue;
                const auto delta = Vector(bullet.direction);
                bullet.x += delta[0] * 300.0f * dt;
                bullet.y += delta[1] * 300.0f * dt;
                const Box box = bullet.Bounds();
                if (box.x < 0 || box.y < 0 || box.x + box.w > Field || box.y + box.h > Field) {
                    bullet.alive = false; continue;
                }
                bool hit = false;
                const int left = static_cast<int>(box.x) / Tile;
                const int right = static_cast<int>(box.x + box.w - 0.01f) / Tile;
                const int top = static_cast<int>(box.y) / Tile;
                const int bottom = static_cast<int>(box.y + box.h - 0.01f) / Tile;
                for (int y = top; y <= bottom && !hit; ++y) {
                    for (int x = left; x <= right && !hit; ++x) {
                        int& wall = walls[static_cast<size_t>(y * Grid + x)];
                        if (wall > 0) { --wall; hit = true; }
                    }
                }
                if (hit) {
                    bullet.alive = false;
                    effects.push_back(Effect::Brick);
                    continue;
                }
                if (Overlaps(box, Base())) {
                    bullet.alive = false;
                    if (!bullet.friendly) {
                        baseAlive = false;
                        blasts.push_back({ 13.0f * Tile, 25.0f * Tile, 0.7f });
                        Lose();
                    }
                    continue;
                }
                if (bullet.friendly) {
                    for (Tank& enemy : enemies) {
                        if (enemy.alive && Overlaps(box, enemy.Bounds())) {
                            enemy.alive = false; bullet.alive = false;
                            ++destroyed; score += 100;
                            blasts.push_back({ enemy.x, enemy.y, 0.45f });
                            effects.push_back(Effect::Explosion);
                            break;
                        }
                    }
                }
                else if (player.alive && Overlaps(box, player.Bounds())) {
                    bullet.alive = false;
                    if (shield <= 0) {
                        player.alive = false; --lives; respawn = 1.2f;
                        blasts.push_back({ player.x, player.y, 0.55f });
                        effects.push_back(Effect::Explosion);
                        if (lives <= 0) Lose();
                    }
                }
            }
            for (size_t i = 0; i < bullets.size(); ++i)
                for (size_t j = i + 1; j < bullets.size(); ++j)
                    if (bullets[i].alive && bullets[j].alive &&
                        bullets[i].friendly != bullets[j].friendly &&
                        Overlaps(bullets[i].Bounds(), bullets[j].Bounds()))
                        bullets[i].alive = bullets[j].alive = false;
            bullets.erase(std::remove_if(bullets.begin(), bullets.end(),
                [](const Bullet& b) { return !b.alive; }), bullets.end());
            enemies.erase(std::remove_if(enemies.begin(), enemies.end(),
                [](const Tank& t) { return !t.alive; }), enemies.end());
        }
        void Tick(float dt, const Input& input) {
            for (Blast& blast : blasts) blast.time -= dt;
            blasts.erase(std::remove_if(blasts.begin(), blasts.end(),
                [](const Blast& b) { return b.time <= 0; }), blasts.end());
            if (mode == Mode::Intermission) {
                transition -= dt;
                if (transition <= 0) { ++round; BeginRound(); }
                return;
            }
            if (mode != Mode::Playing) return;
            shield = std::max(0.0f, shield - dt);
            player.fireTimer = std::max(0.0f, player.fireTimer - dt);
            if (!player.alive) {
                respawn -= dt;
                if (respawn <= 0) {
                    for (int col : {10, 15, 8, 17}) {
                        Tank candidate{ (col + 0.5f) * Tile, 24.5f * Tile,
                                       Direction::Up, 0, 0, 0, true };
                        if (CanOccupy(candidate.Bounds(), 0)) {
                            player = candidate; shield = 2.5f; break;
                        }
                    }
                }
            }
            else {
                if (input.moving) Move(player, input.direction, 100.0f * dt);
                if (input.fire) Fire(player, true);
            }
            spawnTimer -= dt;
            if (spawnTimer <= 0 && spawned < EnemiesPerRound && enemies.size() < 4) {
                SpawnEnemy();
                spawnTimer = 1.1f;
            }
            for (Tank& enemy : enemies) {
                enemy.fireTimer = std::max(0.0f, enemy.fireTimer - dt);
                enemy.turnTimer -= dt;
                if (enemy.turnTimer <= 0) {
                    const int choice = Random(10);
                    if (choice < 5) enemy.direction = Direction::Down;
                    else if (choice < 8)
                        enemy.direction = enemy.x < 13.0f * Tile ? Direction::Right : Direction::Left;
                    else enemy.direction = static_cast<Direction>(Random(4));
                    enemy.turnTimer = 0.5f + Random(100) / 100.0f;
                }
                const float speed = 48.0f + static_cast<float>(std::min(round - 1, 10)) * 4.0f;
                if (!Move(enemy, enemy.direction, speed * dt)) {
                    // Fire into a blocking wall before choosing another route.
                    Fire(enemy, false);
                    enemy.direction = static_cast<Direction>(Random(4));
                    enemy.turnTimer = 0.25f;
                }
                Fire(enemy, false);
            }
            UpdateBullets(dt);
            if (mode == Mode::Playing && destroyed == EnemiesPerRound && enemies.empty()) {
                score += 500;
                bullets.clear();
                mode = Mode::Intermission; transition = 2.5f;
                effects.push_back(Effect::Round);
            }
        }
    private:
        std::mt19937 random;
    };

    class Audio {
    public:
        bool muted = false;
        void Initialize(HWND window) {
            owner = window;
            Make(Effect::Fire, 0.10f, 900, 220, false);
            Make(Effect::Brick, 0.06f, 180, 60, true);
            Make(Effect::Explosion, 0.30f, 140, 30, true);
            Make(Effect::Round, 0.60f, 440, 880, false);
            Make(Effect::GameOver, 0.75f, 440, 90, false);
        }
        void Play(Effect effect) {
            if (muted || failed) return;
            const auto& wav = waves[static_cast<size_t>(effect)];
            if (!PlaySoundW(reinterpret_cast<LPCWSTR>(wav.data()), nullptr,
                SND_MEMORY | SND_ASYNC | SND_NODEFAULT)) {
                failed = true;
                MessageBoxW(owner, L"Sound playback is unavailable. The game will continue without sound.",
                    L"Tank Battalion - audio", MB_OK | MB_ICONWARNING);
            }
        }
        void Toggle() {
            muted = !muted;
            if (muted) PlaySoundW(nullptr, nullptr, 0);
        }
        ~Audio() { PlaySoundW(nullptr, nullptr, 0); }
    private:
        HWND owner = nullptr;
        bool failed = false;
        std::array<std::vector<unsigned char>, static_cast<size_t>(Effect::Count)> waves;
        static void Put32(std::vector<unsigned char>& bytes, size_t at, uint32_t value) {
            for (size_t i = 0; i < 4; ++i) bytes[at + i] = static_cast<unsigned char>(value >> (8 * i));
        }
        static void Put16(std::vector<unsigned char>& bytes, size_t at, uint16_t value) {
            bytes[at] = static_cast<unsigned char>(value);
            bytes[at + 1] = static_cast<unsigned char>(value >> 8);
        }
        void Make(Effect effect, float duration, float start, float end, bool noise) {
            constexpr uint32_t rate = 22050;
            const uint32_t samples = static_cast<uint32_t>(duration * rate);
            auto& bytes = waves[static_cast<size_t>(effect)];
            bytes.resize(44 + samples * 2);
            std::memcpy(bytes.data(), "RIFF", 4);
            Put32(bytes, 4, 36 + samples * 2);
            std::memcpy(bytes.data() + 8, "WAVEfmt ", 8);
            Put32(bytes, 16, 16); Put16(bytes, 20, 1); Put16(bytes, 22, 1);
            Put32(bytes, 24, rate); Put32(bytes, 28, rate * 2);
            Put16(bytes, 32, 2); Put16(bytes, 34, 16);
            std::memcpy(bytes.data() + 36, "data", 4); Put32(bytes, 40, samples * 2);
            uint32_t state = 42;
            double phase = 0;
            for (uint32_t i = 0; i < samples; ++i) {
                const float progress = static_cast<float>(i) / static_cast<float>(samples);
                phase += 6.28318530718 * (start + (end - start) * progress) / rate;
                state = state * 1664525u + 1013904223u;
                const double hiss = static_cast<double>((state >> 16) & 65535u) / 32767.5 - 1.0;
                const double signal = noise ? 0.7 * hiss + 0.3 * std::sin(phase) : std::sin(phase);
                const double envelope = std::min(1.0f, progress * 30) * (1 - progress) * (1 - progress);
                const int16_t sample = static_cast<int16_t>(signal * envelope * 11000);
                Put16(bytes, 44 + i * 2, static_cast<uint16_t>(sample));
            }
        }
    };

    class Scores {
    public:
        int best = 0, last = 0, games = 0;
        void Load(HWND window) {
            owner = window;
            PWSTR folder = nullptr;
            const HRESULT result = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &folder);
            if (FAILED(result)) { Warn(L"Could not locate Local AppData. Scores cannot be saved."); return; }
            const std::filesystem::path directory = std::filesystem::path(folder) / L"TankBattalionNative1980";
            CoTaskMemFree(folder);
            std::error_code error;
            std::filesystem::create_directories(directory, error);
            if (error) { Warn(L"Could not create the score directory. Scores cannot be saved."); return; }
            file = directory / L"scores.txt";
            const bool exists = std::filesystem::exists(file, error);
            if (error) { Warn(L"Could not access the score file. Scores cannot be saved."); return; }
            if (exists) {
                std::ifstream stream(file);
                std::string magic, extra;
                int version = 0;
                if (!(stream >> magic >> version >> best >> last >> games) ||
                    magic != "TANK_SCORES" || version != 1 || best < 0 || last < 0 ||
                    games < 0 || last > best || (stream >> extra)) {
                    best = last = games = 0;
                    Warn(L"The score file is unreadable or invalid. It will not be overwritten.\n"
                        L"To enable saving again, remove %LOCALAPPDATA%\\TankBattalionNative1980\\scores.txt and restart.");
                    return;
                }
            }
            enabled = true;
        }
        void Record(int score) {
            best = std::max(best, score); last = score; ++games;
            if (!enabled) return;
            const std::filesystem::path temp = file.wstring() + L".tmp";
            {
                std::ofstream stream(temp, std::ios::trunc);
                stream << "TANK_SCORES 1\n" << best << '\n' << last << '\n' << games << '\n';
                stream.flush();
                if (!stream) {
                    Warn(L"Could not write the score file. This session's scores are only in memory.");
                    enabled = false; return;
                }
                stream.close();
                if (!stream) {
                    Warn(L"Could not close the score file. This session's scores are only in memory.");
                    enabled = false; return;
                }
            }
            if (!MoveFileExW(temp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                Warn(L"Could not replace the score file. This session's scores are only in memory.");
                enabled = false;
            }
        }
    private:
        HWND owner = nullptr;
        bool enabled = false;
        std::filesystem::path file;
        void Warn(const wchar_t* message) {
            MessageBoxW(owner, message, L"Tank Battalion - score storage", MB_OK | MB_ICONWARNING);
        }
    };

    class Renderer {
    public:
        ~Renderer() {
            if (dc) {
                SelectObject(dc, oldBitmap);
                DeleteObject(bitmap);
                DeleteDC(dc);
            }
            if (font) DeleteObject(font);
            if (largeFont) DeleteObject(largeFont);
        }
        bool Initialize(HWND window) {
            HDC screen = GetDC(window);
            if (!screen) return false;
            dc = CreateCompatibleDC(screen);
            bitmap = CreateCompatibleBitmap(screen, Width, Height);
            ReleaseDC(window, screen);
            if (!dc || !bitmap) {
                if (bitmap) DeleteObject(bitmap);
                if (dc) DeleteDC(dc);
                bitmap = nullptr; dc = nullptr;
                return false;
            }
            oldBitmap = SelectObject(dc, bitmap);
            font = CreateFontW(19, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
            largeFont = CreateFontW(32, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
            return font && largeFont;
        }
        void Paint(HDC target, RECT client, const Game& game, const Scores& scores, bool paused, bool muted) {
            Fill(0, 0, Width, Height, RGB(16, 20, 27));
            Fill(MapX - 3, MapY - 3, Field + 6, Field + 6, RGB(76, 83, 94));
            Fill(MapX, MapY, Field, Field, RGB(8, 11, 15));
            Text(24, 19, L"TANK BATTALION", RGB(238, 202, 85), true);
            Text(430, 27, L"WIN32  /  BASE DEFENSE", RGB(151, 163, 175));
            for (int row = 0; row < Grid; ++row) {
                for (int col = 0; col < Grid; ++col) {
                    const int wall = game.walls[static_cast<size_t>(row * Grid + col)];
                    if (!wall) continue;
                    const int x = MapX + col * Tile, y = MapY + row * Tile;
                    const COLORREF brick = wall == 2 ? RGB(169, 74, 45) : RGB(105, 55, 39);
                    Fill(x, y, Tile - 1, Tile - 1, brick);
                    Fill(x, y, Tile - 1, 2, RGB(214, 117, 70));
                    Fill(x, y + 11, Tile - 1, 2, RGB(39, 27, 24));
                    Fill(x + 11, y, 2, 11, RGB(39, 27, 24));
                    Fill(x + 5, y + 13, 2, 10, RGB(39, 27, 24));
                    Fill(x + 17, y + 13, 2, 10, RGB(39, 27, 24));
                }
            }
            DrawBase(game.baseAlive);
            for (const Tank& enemy : game.enemies) DrawTank(enemy, RGB(219, 91, 77));
            if (game.player.alive && game.mode != Mode::Title) {
                DrawTank(game.player, RGB(237, 204, 84));
                if (game.shield > 0) {
                    const int x = MapX + static_cast<int>(game.player.x);
                    const int y = MapY + static_cast<int>(game.player.y);
                    Frame(x - 14, y - 14, 28, 28, RGB(96, 201, 233));
                }
            }
            for (const Bullet& bullet : game.bullets) {
                const Box box = bullet.Bounds();
                Fill(MapX + static_cast<int>(box.x), MapY + static_cast<int>(box.y),
                    static_cast<int>(box.w), static_cast<int>(box.h), RGB(255, 246, 190));
            }
            for (const Blast& blast : game.blasts) {
                const int radius = 6 + static_cast<int>((0.7f - blast.time) * 22);
                const int x = MapX + static_cast<int>(blast.x), y = MapY + static_cast<int>(blast.y);
                Fill(x - radius, y - 4, radius * 2, 8, RGB(242, 134, 47));
                Fill(x - 4, y - radius, 8, radius * 2, RGB(255, 219, 103));
            }
            const int side = MapX + Field + 20;
            Text(side, 76, L"SCORE", RGB(151, 163, 175));
            Text(side, 100, std::to_wstring(game.score), RGB(244, 242, 218), true);
            Text(side, 157, L"BEST", RGB(151, 163, 175));
            Text(side, 182, std::to_wstring(std::max(game.score, scores.best)), RGB(238, 202, 85));
            Text(side, 231, L"ROUND  " + std::to_wstring(game.round), RGB(244, 242, 218));
            Text(side, 265, L"LIVES  " + std::to_wstring(game.lives), RGB(244, 242, 218));
            Text(side, 313, L"ENEMIES", RGB(151, 163, 175));
            Text(side, 340, std::to_wstring(EnemiesPerRound - game.destroyed) + L" / 20", RGB(219, 91, 77));
            Text(side, 391, L"LAST " + std::to_wstring(scores.last), RGB(151, 163, 175));
            Text(side, 425, L"ARROWS Move", RGB(151, 163, 175));
            Text(side, 451, L"SPACE  Fire", RGB(151, 163, 175));
            Text(side, 477, L"P      Pause", RGB(151, 163, 175));
            Text(side, 503, L"M      Sound", RGB(151, 163, 175));
            Text(side, 529, L"ESC    Exit", RGB(151, 163, 175));
            Text(side, 577, muted ? L"SOUND OFF" : L"SOUND ON", RGB(96, 201, 233));
            Text(24, 696, L"Protect the eagle. Destroy all 20 tanks. Brick walls take two hits.", RGB(151, 163, 175));
            if (game.mode == Mode::Title)
                Overlay(L"DEFEND THE EAGLE", L"ENTER to start  |  3 lives", L"Arrows move   -   Space fires");
            else if (game.mode == Mode::GameOver)
                Overlay(L"GAME OVER", L"Score: " + std::to_wstring(game.score), L"ENTER to play again");
            else if (paused)
                Overlay(L"PAUSED", L"P or ENTER to resume", L"Protect your base!");
            else if (game.mode == Mode::Intermission)
                Overlay(L"ROUND CLEAR", L"+500 bonus points", L"Next round incoming...");
            const int w = client.right - client.left, h = client.bottom - client.top;
            if (w <= 0 || h <= 0) return;
            const double scale = std::min(static_cast<double>(w) / Width, static_cast<double>(h) / Height);
            const int destW = std::max(1, static_cast<int>(Width * scale));
            const int destH = std::max(1, static_cast<int>(Height * scale));
            const int x = (w - destW) / 2, y = (h - destH) / 2;
            // Only letterbox margins are cleared; the playfield is presented in one blit.
            RECT margin{ 0, 0, w, y };
            FillRect(target, &margin, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            margin = { 0, y + destH, w, h };
            FillRect(target, &margin, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            margin = { 0, y, x, y + destH };
            FillRect(target, &margin, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            margin = { x + destW, y, w, y + destH };
            FillRect(target, &margin, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            SetStretchBltMode(target, COLORONCOLOR);
            StretchBlt(target, x, y, destW, destH, dc, 0, 0, Width, Height, SRCCOPY);
        }
    private:
        HDC dc = nullptr;
        HBITMAP bitmap = nullptr;
        HGDIOBJ oldBitmap = nullptr;
        HFONT font = nullptr, largeFont = nullptr;
        void Fill(int x, int y, int w, int h, COLORREF color) {
            SetDCBrushColor(dc, color);
            RECT rect{ x, y, x + w, y + h };
            FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        }
        void Frame(int x, int y, int w, int h, COLORREF color) {
            Fill(x, y, w, 2, color); Fill(x, y + h - 2, w, 2, color);
            Fill(x, y, 2, h, color); Fill(x + w - 2, y, 2, h, color);
        }
        void Text(int x, int y, const std::wstring& text, COLORREF color, bool large = false) {
            const HGDIOBJ old = SelectObject(dc, large ? largeFont : font);
            SetBkMode(dc, TRANSPARENT); SetTextColor(dc, color);
            TextOutW(dc, x, y, text.c_str(), static_cast<int>(text.size()));
            SelectObject(dc, old);
        }
        void DrawTank(const Tank& tank, COLORREF color) {
            const int cx = MapX + static_cast<int>(tank.x), cy = MapY + static_cast<int>(tank.y);
            const bool vertical = tank.direction == Direction::Up || tank.direction == Direction::Down;
            if (vertical) {
                Fill(cx - 10, cy - 10, 5, 20, RGB(103, 109, 111));
                Fill(cx + 5, cy - 10, 5, 20, RGB(103, 109, 111));
                for (int n = -8; n <= 8; n += 4) {
                    Fill(cx - 10, cy + n, 5, 1, RGB(27, 31, 36));
                    Fill(cx + 5, cy + n, 5, 1, RGB(27, 31, 36));
                }
            }
            else {
                Fill(cx - 10, cy - 10, 20, 5, RGB(103, 109, 111));
                Fill(cx - 10, cy + 5, 20, 5, RGB(103, 109, 111));
                for (int n = -8; n <= 8; n += 4) {
                    Fill(cx + n, cy - 10, 1, 5, RGB(27, 31, 36));
                    Fill(cx + n, cy + 5, 1, 5, RGB(27, 31, 36));
                }
            }
            Fill(cx - 6, cy - 6, 12, 12, color);
            Fill(cx - 3, cy - 3, 6, 6, RGB(250, 230, 157));
            const auto direction = Vector(tank.direction);
            for (int n = 3; n <= 13; ++n)
                Fill(cx + static_cast<int>(direction[0] * n) - 1,
                    cy + static_cast<int>(direction[1] * n) - 1, 3, 3, color);
        }
        void DrawBase(bool alive) {
            const int x = MapX + 12 * Tile, y = MapY + 24 * Tile;
            Fill(x + 2, y + 2, 44, 44, RGB(35, 42, 47));
            const COLORREF color = alive ? RGB(221, 224, 213) : RGB(98, 59, 53);
            Fill(x + 21, y + 9, 8, 29, color);
            Fill(x + 10, y + 15, 29, 7, color);
            Fill(x + 6, y + 12, 9, 6, color);
            Fill(x + 34, y + 12, 9, 6, color);
            Fill(x + 17, y + 31, 16, 4, color);
            Fill(x + 27, y + 10, 7, 4, RGB(238, 202, 85));
        }
        void Overlay(const std::wstring& title, const std::wstring& line, const std::wstring& hint) {
            Fill(MapX + 44, MapY + 235, Field - 88, 150, RGB(20, 26, 34));
            Frame(MapX + 44, MapY + 235, Field - 88, 150, RGB(238, 202, 85));
            Text(MapX + 70, MapY + 254, title, RGB(238, 202, 85), true);
            Text(MapX + 70, MapY + 303, line, RGB(244, 242, 218));
            Text(MapX + 70, MapY + 338, hint, RGB(151, 163, 175));
        }
    };

    class Application {
    public:
        Game game{ static_cast<unsigned int>(GetTickCount64()) };
        Audio audio;
        Scores scores;
        Renderer renderer;
        HWND window = nullptr;
        bool paused = false, recorded = true;
        std::array<bool, 256> keys{};
        Direction latest = Direction::Up;
        std::chrono::steady_clock::time_point previous = std::chrono::steady_clock::now();
        double accumulator = 0;
        void FinishScore() {
            if (!recorded) { scores.Record(game.score); recorded = true; }
        }
        void Start() {
            FinishScore();
            game.Start(); paused = false; recorded = false;
            game.effects.clear(); keys.fill(false); accumulator = 0;
            previous = std::chrono::steady_clock::now();
        }
        void ClearInput() { keys.fill(false); accumulator = 0; }
        Input ReadInput() const {
            Input input;
            input.fire = keys[VK_SPACE];
            const std::array<int, 4> codes{ VK_UP, VK_RIGHT, VK_DOWN, VK_LEFT };
            if (keys[static_cast<size_t>(codes[static_cast<size_t>(latest)])]) {
                input.moving = true; input.direction = latest;
            }
            else {
                for (size_t i = 0; i < codes.size(); ++i)
                    if (keys[static_cast<size_t>(codes[i])]) {
                        input.moving = true; input.direction = static_cast<Direction>(i); break;
                    }
            }
            return input;
        }
        void Timer() {
            const auto now = std::chrono::steady_clock::now();
            const double elapsed = std::chrono::duration<double>(now - previous).count();
            previous = now;
            if (!paused && !IsIconic(window)) {
                accumulator += std::min(elapsed, 0.10);
                while (accumulator >= Step) {
                    game.Tick(Step, ReadInput());
                    accumulator -= Step;
                }
                if (game.mode == Mode::GameOver) FinishScore();
                if (!game.effects.empty()) {
                    const Effect sound = *std::max_element(game.effects.begin(), game.effects.end());
                    audio.Play(sound);
                    game.effects.clear();
                }
            }
            else accumulator = 0;
            InvalidateRect(window, nullptr, FALSE);
        }
    };

    LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        auto* app = reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            app = static_cast<Application*>(create->lpCreateParams);
            app->window = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }
        if (!app) return DefWindowProcW(window, message, wParam, lParam);
        switch (message) {
        case WM_ERASEBKGND: return 1;
        case WM_TIMER:
            app->Timer(); return 0;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            RECT client{}; GetClientRect(window, &client);
            app->renderer.Paint(dc, client, app->game, app->scores, app->paused, app->audio.muted);
            EndPaint(window, &paint); return 0;
        }
        case WM_KEYDOWN:
            if (wParam < app->keys.size()) app->keys[static_cast<size_t>(wParam)] = true;
            if (wParam == VK_UP) app->latest = Direction::Up;
            if (wParam == VK_RIGHT) app->latest = Direction::Right;
            if (wParam == VK_DOWN) app->latest = Direction::Down;
            if (wParam == VK_LEFT) app->latest = Direction::Left;
            if ((lParam & (1LL << 30)) != 0) return 0;
            if (wParam == VK_ESCAPE) SendMessageW(window, WM_CLOSE, 0, 0);
            else if (wParam == 'M') app->audio.Toggle();
            else if (wParam == VK_RETURN) {
                if (app->game.mode == Mode::Title || app->game.mode == Mode::GameOver) app->Start();
                else if (app->paused) { app->paused = false; app->ClearInput(); }
            }
            else if (wParam == 'P' &&
                (app->game.mode == Mode::Playing || app->game.mode == Mode::Intermission)) {
                app->paused = !app->paused; app->ClearInput();
            }
            return 0;
        case WM_KEYUP:
            if (wParam < app->keys.size()) app->keys[static_cast<size_t>(wParam)] = false;
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE) {
                app->ClearInput();
                if (app->game.mode == Mode::Playing || app->game.mode == Mode::Intermission)
                    app->paused = true;
            }
            return 0;
        case WM_SIZE:
            if (wParam == SIZE_MINIMIZED) {
                app->ClearInput();
                if (app->game.mode == Mode::Playing || app->game.mode == Mode::Intermission)
                    app->paused = true;
            }
            InvalidateRect(window, nullptr, FALSE); return 0;
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize = { 600, 530 }; return 0;
        }
        case WM_CLOSE:
            KillTimer(window, 1);
            app->FinishScore();
            DestroyWindow(window); return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        default: return DefWindowProcW(window, message, wParam, lParam);
        }
    }

    int SelfTest() {
        int checks = 0, failures = 0;
        std::ostringstream report;
        auto check = [&](bool condition, const char* name) {
            ++checks;
            if (!condition) { ++failures; report << "FAIL: " << name << "\n"; }
            };
        Game game(123);
        game.Start();
        check(game.mode == Mode::Playing && game.lives == 3 && game.score == 0, "new game");
        check(game.walls[23 * Grid + 12] == 2 && !game.WallAt(game.player.Bounds()), "base enclosure and spawn");
        check(game.WallAt({ -1, 0, 20, 20 }) && game.WallAt(Game::Base()), "boundary and base collision");
        game.walls.fill(0);
        game.walls[10 * Grid + 10] = 2;
        Tank wallProbe{ 10.5f * Tile, 11.5f * Tile, Direction::Up, 0, 0, 50, true };
        check(!game.Move(wallProbe, Direction::Up, 10), "tank cannot move into bricks");
        game.walls.fill(0);
        Tank edgeProbe{ 10, 100, Direction::Left, 0, 0, 50, true };
        check(!game.Move(edgeProbe, Direction::Left, 1), "tank cannot leave field");
        Tank turnProbe{ 100, 100, Direction::Down, 0, 0, 50, true };
        check(game.Move(turnProbe, Direction::Right, 1) && turnProbe.y == 100, "turn without near-center snap");
        turnProbe.y = 106;
        check(game.Move(turnProbe, Direction::Right, 1) && turnProbe.y == 108, "corridor turn snaps near center");
        game.enemies.push_back({ 200, 200, Direction::Down, 0, 0, 1, true });
        check(!game.CanOccupy({ 195, 195, 20, 20 }, 0), "tanks block one another");
        game.enemies.clear();
        const float before = game.player.x;
        game.Tick(Step, { true, false, Direction::Right });
        check(game.player.x > before, "arrow movement");
        game.Fire(game.player, true); game.player.fireTimer = 0; game.Fire(game.player, true);
        check(game.bullets.size() == 1, "single player projectile");
        game.bullets.clear();
        game.walls[5 * Grid + 5] = 2;
        game.bullets.push_back({ 5.5f * Tile, 5.5f * Tile, Direction::Up, true, true, 0 });
        game.UpdateBullets(Step);
        check(game.walls[5 * Grid + 5] == 1 && game.bullets.empty(), "first brick hit");
        game.bullets.push_back({ 5.5f * Tile, 5.5f * Tile, Direction::Up, true, true, 0 });
        game.UpdateBullets(Step);
        check(game.walls[5 * Grid + 5] == 0, "second brick hit");
        game.enemies.push_back({ 100, 100, Direction::Down, 0, 0, 1, true });
        game.bullets.push_back({ 100, 100, Direction::Up, true, true, 0 });
        game.UpdateBullets(Step);
        check(game.enemies.empty() && game.score == 100 && game.destroyed == 1, "enemy kill and score");
        game.shield = 1;
        game.bullets.push_back({ game.player.x, game.player.y, Direction::Down, false, true, 3 });
        game.UpdateBullets(Step);
        check(game.player.alive && game.lives == 3 && game.bullets.empty(), "respawn shield");
        game.shield = 0;
        game.bullets.push_back({ game.player.x, game.player.y, Direction::Down, false, true, 3 });
        game.UpdateBullets(Step);
        check(!game.player.alive && game.lives == 2, "player hit costs one life");
        game.spawned = EnemiesPerRound; game.respawn = 0;
        game.Tick(Step, {});
        check(game.player.alive && game.shield > 0, "player respawn");
        game.bullets.push_back({ 150, 150, Direction::Up, true, true, 0 });
        game.bullets.push_back({ 150, 150, Direction::Down, false, true, 3 });
        game.UpdateBullets(Step);
        check(game.bullets.empty(), "opposing projectiles cancel");
        game.bullets.push_back({ 2, 2, Direction::Up, true, true, 0 });
        game.UpdateBullets(Step);
        check(game.bullets.empty(), "out-of-bounds projectile removed");
        game.destroyed = EnemiesPerRound;
        game.Tick(Step, {});
        check(game.mode == Mode::Intermission && game.score == 600, "round clear bonus");
        game.Tick(3, {});
        check(game.round == 2 && game.mode == Mode::Playing && game.destroyed == 0 && game.score == 600,
            "next round preserves score");
        game.walls.fill(0);
        game.bullets.push_back({ 13.0f * Tile, 25.0f * Tile, Direction::Down, true, true, 0 });
        game.UpdateBullets(Step);
        check(game.baseAlive && game.mode == Mode::Playing, "friendly shot does not destroy base");
        game.bullets.push_back({ 13.0f * Tile, 25.0f * Tile, Direction::Down, false, true, 3 });
        game.UpdateBullets(Step);
        check(!game.baseAlive && game.mode == Mode::GameOver, "base loss ends game");
        game.Start(); game.shield = 0; game.lives = 1;
        game.bullets.push_back({ game.player.x, game.player.y, Direction::Down, false, true, 3 });
        game.UpdateBullets(Step);
        check(game.lives == 0 && game.mode == Mode::GameOver, "last life ends game");
        game.Start(); game.walls.fill(0);
        for (int i = 0; i < 20; ++i) game.SpawnEnemy();
        check(game.enemies.size() == 3 && game.spawned == 3, "occupied spawns are not overlapped");
        game.Start();
        size_t maximum = 0;
        for (int i = 0; i < 120 * 90; ++i) {
            game.Tick(Step, {});
            maximum = std::max(maximum, game.enemies.size());
            if (game.mode == Mode::GameOver) break;
        }
        check(maximum <= 4 && game.spawned <= EnemiesPerRound, "enemy caps during simulated play");
        game.Start();
        bool valid = true;
        for (int i = 0; i < 120 * 180; ++i) {
            const Direction direction = static_cast<Direction>((i / 97) % 4);
            game.Tick(Step, { true, true, direction });
            if (game.player.alive) valid = valid && !game.WallAt(game.player.Bounds());
            for (const Tank& enemy : game.enemies)
                valid = valid && !game.WallAt(enemy.Bounds());
            valid = valid && game.enemies.size() <= 4 && game.bullets.size() <= 5 &&
                game.spawned <= EnemiesPerRound && game.lives >= 0;
            game.effects.clear();
            if (game.mode == Mode::GameOver) game.Start();
        }
        check(valid, "three-minute simulated movement preserves collision and entity invariants");
        report << checks << " checks, " << failures << " failures\n";
        const std::string text = report.str();
        DWORD written = 0;
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
        return failures == 0 ? 0 : 1;
    }
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE , PWSTR , int show) {
    int count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) {
        MessageBoxW(nullptr, L"Could not parse command-line arguments.", L"Tank Battalion", MB_OK | MB_ICONERROR);
        return 1;
    }
    const bool selfTest = count == 2 && std::wstring(arguments[1]) == L"--self-test";
    LocalFree(arguments);
    if (selfTest) return SelfTest();
    if (count != 1) {
        MessageBoxW(nullptr, L"Usage: TankBattalion.exe [--self-test]", L"Tank Battalion", MB_OK | MB_ICONERROR);
        return 2;
    }
    SetProcessDPIAware();
    Application app;
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls); cls.hInstance = instance;
    cls.lpfnWndProc = WindowProcedure; cls.lpszClassName = L"TankBattalionWin32";
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    if (!RegisterClassExW(&cls)) {
        MessageBoxW(nullptr, L"Could not register the window class.", L"Tank Battalion", MB_OK | MB_ICONERROR);
        return 1;
    }
    RECT rect{ 0, 0, Width, Height };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    HWND window = CreateWindowExW(0, cls.lpszClassName, L"Tank Battalion - Native Win32",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, instance, &app);
    if (!window) {
        MessageBoxW(nullptr, L"Could not create the game window.", L"Tank Battalion", MB_OK | MB_ICONERROR);
        return 1;
    }
    if (!app.renderer.Initialize(window)) {
        MessageBoxW(window, L"Could not allocate the graphics back buffer or fonts.",
            L"Tank Battalion", MB_OK | MB_ICONERROR);
        DestroyWindow(window); return 1;
    }
    ShowWindow(window, show);
    UpdateWindow(window);
    app.scores.Load(window);
    app.audio.Initialize(window);
    if (!SetTimer(window, 1, 8, nullptr)) {
        MessageBoxW(window, L"Could not start the game timer.", L"Tank Battalion", MB_OK | MB_ICONERROR);
        DestroyWindow(window); return 1;
    }
    MSG message{};
    BOOL result = 0;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (result == -1) {
        MessageBoxW(window, L"The Windows message loop failed.", L"Tank Battalion", MB_OK | MB_ICONERROR);
        app.FinishScore();
        DestroyWindow(window); return 1;
    }
    return static_cast<int>(message.wParam);
}
