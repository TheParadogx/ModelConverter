#include"Converter.h"

#include<iostream>
#include<string>
#include<filesystem>
#include<charconv>

namespace fs = std::filesystem;

static void PrintHelp(const char* exe)
{
    std::cout
        << "Usage: " << exe << " <input> [output_dir] [options]\n\n"
        << "Options:\n"
        << "  --scale <val>     スケール倍率 (default: 1.0)\n"
        << "  --bake-fps <val>  アニメーションベイクFPS (default: 30, 0=スパース)\n"
        << "  --no-lh           左手系変換を無効化\n"
        << "  --no-flip-uvs     UV フリップを無効化\n"
        << "  --no-optimize     メッシュ最適化を無効化\n"
        << "  -v, --verbose     詳細ログ\n"
        << "  -h, --help        このヘルプ\n\n"
        << "Examples:\n"
        << "  " << exe << " character.fbx ./out --scale 0.01 -v\n"
        << "  " << exe << " character.fbx ./out --bake-fps 60\n"
        << "  " << exe << " mesh.obj ./out --bake-fps 0\n";
}

#ifdef _WIN32
#include <windows.h>
static void EnableANSI()
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0; GetConsoleMode(h, &mode);
    SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}
#else
static void EnableANSI() {}
#endif

static const char* COL_GREEN = "\033[32m";
static const char* COL_RED = "\033[31m";
static const char* COL_YELLOW = "\033[33m";
static const char* COL_RESET = "\033[0m";

struct Args
{
    bool valid = false;
    bool showHelp = false;
    fs::path inputPath;
    fs::path outputDir;
    conv::ConverterOptions options;
};

static Args ParseArgs(int argc, char* argv[])
{
    Args args;
    if (argc < 2) return args;

    std::string first = argv[1];
    if (first == "-h" || first == "--help") { args.showHelp = args.valid = true; return args; }

    args.inputPath = argv[1];
    int i = 2;
    if (argc > 2 && argv[2][0] != '-') { args.outputDir = argv[2]; i = 3; }

    for (; i < argc; ++i)
    {
        std::string opt = argv[i];
        auto parseFloat = [&](float& out) -> bool {
            if (i + 1 >= argc) return false;
            std::string s = argv[++i];
            auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
            return ec == std::errc{};
            };

        if (opt == "--no-lh")       args.options.convertToLeftHanded = false;
        else if (opt == "--no-flip-uvs") args.options.flipUVs = false;
        else if (opt == "--no-optimize") { args.options.optimizeMeshes = args.options.joinIdenticalVertices = false; }
        else if (opt == "-v" || opt == "--verbose") args.options.verbose = true;
        else if (opt == "-h" || opt == "--help") { args.showHelp = args.valid = true; return args; }
        else if (opt == "--scale")
        {
            if (!parseFloat(args.options.scale)) { std::cerr << COL_RED << "--scale: 値が不正\n" << COL_RESET; return args; }
        }
        else if (opt == "--bake-fps")
        {
            if (!parseFloat(args.options.bakeFPS)) { std::cerr << COL_RED << "--bake-fps: 値が不正\n" << COL_RESET; return args; }
        }
        else
            std::cerr << COL_YELLOW << "[WARN] 不明なオプション: " << opt << COL_RESET << "\n";
    }

    args.valid = true;
    return args;
}

#ifdef _DEBUG

// デバック用のパス
static constexpr const char* DEBUG_INPUT_PATH = R"(C:\Models\character.fbx)";
static constexpr const char* DEBUG_OUTPUT_DIR = R"(C:\Models\output)";

static conv::ConverterOptions MakeDebugOptions()
{
    conv::ConverterOptions opt;
    opt.convertToLeftHanded = true;
    opt.flipUVs = true;
    opt.generateNormals = true;
    opt.generateTangents = true;
    opt.optimizeMeshes = true;
    opt.scale = 1.0f;
    opt.bakeFPS = 30.0f;  // デバッグ時も 30fps ベイク
    opt.verbose = true;
    return opt;
}

#endif // _DEBUG

int main(int argc, char* argv[])
{
#ifdef _WIN32
    SetConsoleOutputCP(65001);
    std::setlocale(LC_ALL, ".UTF-8");
#endif

    EnableANSI();
    std::cout << "=== ModelConverter ===\n\n";

#ifdef _DEBUG
    Args args;
    if (argc < 2)
    {
        std::cout << COL_YELLOW << "[DEBUG] 引数なし → デバッグパスを使用\n" << COL_RESET;
        args.inputPath = DEBUG_INPUT_PATH;
        args.outputDir = DEBUG_OUTPUT_DIR;
        args.options = MakeDebugOptions();
        args.valid = true;
    }
    else
    {
        args = ParseArgs(argc, argv);
    }
#else
    Args args = ParseArgs(argc, argv);
#endif

    if (args.showHelp) { PrintHelp(argv[0]); return 0; }
    if (!args.valid || args.inputPath.empty())
    {
        std::cerr << COL_RED << "[ERROR] 入力ファイルを指定してください。\n" << COL_RESET;
        PrintHelp(argv[0]); return 1;
    }
    if (!fs::exists(args.inputPath))
    {
        std::cerr << COL_RED << "[ERROR] ファイルが見つかりません: " << args.inputPath << COL_RESET << "\n";
        return 1;
    }

    std::cout << "入力        : " << fs::absolute(args.inputPath) << "\n";
    std::cout << "出力先      : " << (args.outputDir.empty()
        ? fs::absolute(args.inputPath.parent_path())
        : fs::absolute(args.outputDir)) << "\n";
    std::cout << "スケール    : " << args.options.scale << "\n";
    std::cout << "左手系変換  : " << (args.options.convertToLeftHanded ? "yes" : "no") << "\n";
    std::cout << "UV フリップ : " << (args.options.flipUVs ? "yes" : "no") << "\n";
    std::cout << "最適化      : " << (args.options.optimizeMeshes ? "yes" : "no") << "\n";
    std::cout << "アニメBake  : ";
    if (args.options.bakeFPS > 0.0f)
        std::cout << args.options.bakeFPS << " fps\n";
    else
        std::cout << "スパース (二分探索)\n";
    std::cout << "\n";

    conv::ModelConverter converter;
    converter.SetLogCallback([](const std::string& msg) { std::cout << "  " << msg << "\n"; });

    conv::ConvertResult result = converter.Convert(args.inputPath, args.outputDir, args.options);
    std::cout << "\n";
    if (result.success)
    {
        std::cout << COL_GREEN << "✓ 変換成功\n" << COL_RESET;
        std::cout << "  メッシュ        : " << result.meshCount << "\n";
        std::cout << "  マテリアル      : " << result.materialCount << "\n";
        std::cout << "  ボーン(全ノード): " << result.boneCount << "\n";
        std::cout << "  アニメーション  : " << result.animationCount << "\n\n";
        std::cout << "  [bin] " << result.binPath << "\n";
        if (!result.anmPath.empty())
            std::cout << "  [anm] " << result.anmPath << "\n";
    }
    else
    {
        std::cerr << COL_RED << "✗ 変換失敗: " << result.errorMessage << COL_RESET << "\n";
        return 1;
    }


    return 0;
}