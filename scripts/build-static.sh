#!/usr/bin/env bash
#
# Сборка MineSweeper_Infinity со статической линковкой Qt (MinGW, Windows).
#
# Скрипт делает всё сам:
#   1. проверяет тулчейн и заводит ASCII-junction на исходники;
#   2. ищет статический Qt, а если его нет — собирает из исходников qtbase;
#   3. собирает приложение из проекта static/CMakeLists.txt;
#   4. проверяет, что в exe не осталось зависимостей от DLL Qt.
#
# Примеры:
#   scripts/build-static.sh --build-qt      первый запуск: собрать статический Qt и приложение
#   scripts/build-static.sh                 пересобрать только приложение
#   scripts/build-static.sh --build-qt --qt-only
#                                           собрать только статический Qt
#   scripts/build-static.sh --clean --jobs=8
#   scripts/build-static.sh --help
#
set -euo pipefail

# ------------------------------------------------------------------
# Настройки (всё перекрывается переменными окружения)
# ------------------------------------------------------------------
QT_VERSION="${QT_VERSION:-6.11.1}"
QT_SERIES="${QT_VERSION%.*}"

QT_TOOLS_DIR="${QT_TOOLS_DIR:-C:/Qt/Tools}"
QT_STATIC_DIR="${QT_STATIC_DIR:-C:/Qt/${QT_VERSION}/mingw_64_static}"
QT_SRC_DIR="${QT_SRC_DIR:-C:/u/qt-src}"
APP_BUILD_DIR="${APP_BUILD_DIR:-C:/u/msi-static}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

# Qt 6.11 moc не умеет писать по путям с не-ASCII символами, поэтому и исходники,
# и каталог сборки подставляются через ASCII-junction.
ASCII_PROJECT="${ASCII_PROJECT:-C:/u/MineSweeper_Infinity}"

BUILD_QT=0
QT_ONLY=0
CLEAN=0
RUN_APP=0

# ------------------------------------------------------------------
# Вывод
# ------------------------------------------------------------------
if [ -t 1 ]; then BOLD=$'\033[1m'; RESET=$'\033[0m'; else BOLD=''; RESET=''; fi
step() { printf '\n%s==> %s%s\n' "$BOLD" "$*" "$RESET"; }
info() { printf '    %s\n' "$*"; }
warn() { printf '    [!] %s\n' "$*" >&2; }
die()  { printf '\nОшибка: %s\n' "$*" >&2; exit 1; }

usage() {
    cat <<'EOF'
Сборка MineSweeper_Infinity со статической линковкой Qt.

Использование:
  scripts/build-static.sh [опции]

Опции:
  --build-qt              собрать статический Qt из исходников (долго, ~30-60 мин).
                          Без этой опции скрипт ищет уже собранный статический Qt.
  --qt-static-dir=DIR     каталог установленного статического Qt
                          (по умолчанию C:/Qt/<версия>/mingw_64_static)
  --qt-only               остановиться после сборки статического Qt
  --build-dir=DIR         каталог сборки приложения (по умолчанию C:/u/msi-static)
  --build-type=TYPE       Release (по умолчанию), Debug или Profile
  --jobs=N                число параллельных задач (по умолчанию — все ядра)
  --clean                 удалить каталог сборки приложения перед сборкой
  --run                   запустить собранный exe после проверки.
                          Внимание: игра пишет миры в AppData, а не в проект
  --skip-verify           не проверять зависимости exe
  -h, --help              эта справка

Переменные окружения: QT_VERSION, QT_STATIC_DIR, QT_SRC_DIR, APP_BUILD_DIR,
BUILD_TYPE, JOBS, QT_TOOLS_DIR, ASCII_PROJECT.

Зависимости: Qt Creator с тулчейном MinGW 64-bit (cmake, ninja, g++ в C:/Qt/Tools),
curl, tar, xz. Первый запуск со сборкой Qt требует доступа к download.qt.io.
EOF
}

# ------------------------------------------------------------------
# Разбор аргументов
# ------------------------------------------------------------------
while [ $# -gt 0 ]; do
    case "$1" in
        -h|--help)          usage; exit 0 ;;
        --build-qt)         BUILD_QT=1 ;;
        --qt-only)          QT_ONLY=1; BUILD_QT=1 ;;
        --clean)            CLEAN=1 ;;
        --run)              RUN_APP=1 ;;
        --skip-verify)      SKIP_VERIFY=1 ;;
        --qt-static-dir)    shift; QT_STATIC_DIR="${1:?нужен путь}" ;;
        --qt-static-dir=*)  QT_STATIC_DIR="${1#*=}" ;;
        --build-dir)        shift; APP_BUILD_DIR="${1:?нужен путь}" ;;
        --build-dir=*)      APP_BUILD_DIR="${1#*=}" ;;
        --build-type)       shift; BUILD_TYPE="${1:?нужен тип сборки}" ;;
        --build-type=*)     BUILD_TYPE="${1#*=}" ;;
        --jobs)             shift; JOBS="${1:?нужно число}" ;;
        --jobs=*)           JOBS="${1#*=}" ;;
        *) die "неизвестный аргумент '$1' (см. --help)" ;;
    esac
    shift
done

case "$BUILD_TYPE" in
    Release|Debug|Profile) ;;
    *) die "--build-type должен быть Release, Debug или Profile" ;;
esac

# ------------------------------------------------------------------
# Тулчейн
# ------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REAL_PROJECT="$(cd "$SCRIPT_DIR/.." && pwd -P)"

find_tool() {  # find_tool <имя в C:/Qt/Tools> <имя в PATH> <описание>
    local candidate="$QT_TOOLS_DIR/$1"
    if [ -x "$candidate" ]; then printf '%s\n' "$candidate"; return 0; fi
    command -v "$2" 2>/dev/null && return 0
    die "не найден $3. Ожидался $candidate или $2 в PATH"
}

find_mingw_bin() {
    local dir
    for dir in $(ls -d "$QT_TOOLS_DIR"/mingw*_64 2>/dev/null | sort -Vr); do
        if [ -x "$dir/bin/g++.exe" ]; then printf '%s\n' "$dir/bin"; return 0; fi
    done
    die "не найден MinGW из Qt (ожидался $QT_TOOLS_DIR/mingw*_64/bin/g++.exe)"
}

# cmd.exe не понимает записи вида C:/path в PATH — ему нужен путь через /c/path.
# Аргументам это не требуется, поэтому конвертируем только при формировании PATH.
to_cmd_path() {
    if command -v cygpath >/dev/null 2>&1; then
        cygpath -u "$1"
    else
        printf '%s\n' "$1"
    fi
}

check_toolchain() {
    step 'Проверяю тулчейн'
    CMAKE="$(find_tool CMake_64/bin/cmake.exe cmake cmake)"
    NINJA="$(find_tool Ninja/ninja.exe ninja ninja)"
    MINGW_BIN="$(find_mingw_bin)"
    GXX="$MINGW_BIN/g++.exe"
    OBJDUMP="$MINGW_BIN/objdump.exe"

    [ -x "$OBJDUMP" ] || die "не найден objdump ($OBJDUMP)"

    info "cmake   $("$CMAKE" --version | head -1)"
    info "ninja   $("$NINJA" --version)"
    info "g++     "$("$GXX" --version | head -1)""
    info "ядер    $JOBS"
}

# ------------------------------------------------------------------
# ASCII-junction на исходники
# ------------------------------------------------------------------
ensure_junction() {
    step 'Проверяю ASCII-путь к исходникам'

    if [ -e "$ASCII_PROJECT" ]; then
        local resolved
        resolved="$(cd "$ASCII_PROJECT" && pwd -P)"
        if [ "$resolved" = "$REAL_PROJECT" ]; then
            info "$ASCII_PROJECT -> $REAL_PROJECT"
            return 0
        fi
        die "$ASCII_PROJECT уже существует и ведёт на $resolved, а не на $REAL_PROJECT"
    fi

    mkdir -p "$(dirname "$ASCII_PROJECT")"
    local win_link win_target
    win_link="$(cygpath -w "$(dirname "$ASCII_PROJECT")" 2>/dev/null || echo "$(dirname "$ASCII_PROJECT")")\\$(basename "$ASCII_PROJECT")"
    win_target="$(cygpath -w "$REAL_PROJECT" 2>/dev/null || echo "$REAL_PROJECT")"
    info "создаю junction $win_link -> $win_target"
    cmd //c mklink //J "$win_link" "$win_target" >/dev/null \
        || die "не удалось создать junction (нужны права на запись в $(dirname "$ASCII_PROJECT"))"
    info "$ASCII_PROJECT -> $REAL_PROJECT"
}

# ------------------------------------------------------------------
# Поиск статического Qt
# ------------------------------------------------------------------
is_static_qt() {
    local dir="${1%/}"
    [ -n "$dir" ] && [ -f "$dir/lib/cmake/Qt6/Qt6Config.cmake" ] || return 1
    if [ -f "$dir/lib/Qt6Core.prl" ]; then
        grep -q 'QMAKE_PRL_CONFIG = static' "$dir/lib/Qt6Core.prl" && return 0
        return 1
    fi
    # Запасной признак: есть статическая библиотека и нет DLL рядом с ней.
    [ -f "$dir/lib/libQt6Core.a" ] && [ ! -f "$dir/bin/Qt6Core.dll" ]
}

find_static_qt() {
    local candidates=(
        "$QT_STATIC_DIR"
        "C:/Qt/$QT_VERSION/mingw_64_static"
        "C:/Qt/$QT_VERSION/mingw_64_static_release"
        "C:/Qt/static/$QT_VERSION/mingw_64"
        "C:/u/qt-static/$QT_VERSION/mingw_64"
        "$QT_STATIC_DIR/../../mingw_64_static"
    )
    local extra
    for extra in "C:/Qt/$QT_VERSION"/*static* "C:/Qt/static"/*; do
        [ -d "$extra" ] && candidates+=("$extra")
    done

    local dir
    for dir in "${candidates[@]}"; do
        if is_static_qt "$dir"; then printf '%s\n' "${dir%/}"; return 0; fi
    done
    return 1
}

# ------------------------------------------------------------------
# Сборка статического Qt из исходников
# ------------------------------------------------------------------
QT_SOURCE_MIRRORS=(
    "https://download.qt.io/official_releases/qt/SERIES/VERSION/submodules"
    "https://mirrors.tuna.tsinghua.edu.cn/qt/official_releases/qt/SERIES/VERSION/submodules"
    "https://mirrors.ustc.edu.cn/qtproject/official_releases/qt/SERIES/VERSION/submodules"
    "https://mirrors.aliyun.com/qt/official_releases/qt/SERIES/VERSION/submodules"
)

download_qt_source() {
    local archive="$1"
    local dest="$QT_SRC_DIR/$archive"

    if [ -s "$dest" ] && xz -t "$dest" 2>/dev/null; then
        info "исходники уже скачаны: $dest"
        return 0
    fi
    rm -f "$dest"
    mkdir -p "$QT_SRC_DIR"

    local base url
    for base in "${QT_SOURCE_MIRRORS[@]}"; do
        url="${base/SERIES/$QT_SERIES}"
        url="${url/VERSION/$QT_VERSION}/$archive"
        info "качаю $url"
        if curl -fL --retry 3 --connect-timeout 20 -o "$dest" "$url" \
                && xz -t "$dest" 2>/dev/null; then
            info "готово ($(du -h "$dest" | cut -f1))"
            return 0
        fi
        warn "не вышло с $(echo "$base" | cut -d/ -f3), пробую следующее зеркало"
        rm -f "$dest"
    done
    die "не удалось скачать $archive ни с одного зеркала (нужен доступ к download.qt.io)"
}

build_static_qt() {
    step "Собираю статический Qt $QT_VERSION (это долго)"

    command -v curl >/dev/null || die "для загрузки исходников нужен curl"
    command -v xz   >/dev/null || die "для распаковки нужен xz"
    command -v tar  >/dev/null || die "для распаковки нужен tar"

    local archive="qtbase-everywhere-src-$QT_VERSION.tar.xz"
    local src="$QT_SRC_DIR/qtbase-everywhere-src-$QT_VERSION"

    download_qt_source "$archive"

    if [ ! -d "$src" ]; then
        info "распаковываю в $QT_SRC_DIR"
        xz -dc "$QT_SRC_DIR/$archive" | tar -xf - -C "$QT_SRC_DIR"
    fi
    [ -f "$src/configure.bat" ] || die "в $src нет configure.bat — архив распаковался не так"

    # После неудачного configure в дереве остаётся CMakeCache.txt, из-за которого
    # следующая попытка падает уже по другой причине.
    if [ -f "$src/CMakeCache.txt" ] && [ ! -f "$src/build.ninja" ]; then
        info 'сбрасываю неудачную конфигурацию'
        rm -rf "$src/CMakeCache.txt" "$src/CMakeFiles"
    fi

    if [ ! -f "$src/build.ninja" ]; then
        info "конфигурирую (release, static, без примеров и тестов)"
        # configure.bat — обычный cmd-скрипт, ему нужны cmake, ninja и компилятор
        # в PATH: сам он ничего из этого не ищет.
        local started=$SECONDS
        local qt_tool_path
        qt_tool_path="$(to_cmd_path "$MINGW_BIN"):$(to_cmd_path "$QT_TOOLS_DIR/Ninja"):$(to_cmd_path "$(dirname "$CMAKE")")"
        (
            cd "$src"
            PATH="$qt_tool_path:$PATH" \
            cmd //c configure.bat \
                -static -static-runtime \
                -release -opensource -confirm-license \
                -prefix "$QT_STATIC_DIR" \
                -platform win32-g++ \
                -nomake examples -nomake tests \
                -no-pch
        ) || die "configure завершился с ошибкой (подробности выше и в $src/config.log)"
        info "конфигурация заняла $((SECONDS - started)) с"
    else
        info "конфигурация уже есть, пропускаю (удалите $src/build.ninja для повтора)"
    fi

    info "компилирую, это займёт десятки минут"
    local started=$SECONDS
    "$CMAKE" --build "$src" --parallel "$JOBS"
    info "компиляция заняла $((SECONDS - started)) с"

    info "устанавливаю в $QT_STATIC_DIR"
    "$CMAKE" --install "$src"

    is_static_qt "$QT_STATIC_DIR" \
        || die "после установки $QT_STATIC_DIR не похож на статический Qt"
    info "статический Qt готов: $QT_STATIC_DIR"
}

# ------------------------------------------------------------------
# Сборка приложения
# ------------------------------------------------------------------
configure_app() {
    step "Конфигурирую приложение ($BUILD_TYPE)"

    local src="$ASCII_PROJECT/static"
    [ -f "${REAL_PROJECT}/static/CMakeLists.txt" ] || die "не найден static/CMakeLists.txt"

    local args=(
        -S "$src"
        -B "$APP_BUILD_DIR"
        -G Ninja
        "-DCMAKE_BUILD_TYPE=$BUILD_TYPE"
        "-DCMAKE_PREFIX_PATH=$QT_STATIC_DIR"
        "-DCMAKE_C_COMPILER=$MINGW_BIN/gcc.exe"
        "-DCMAKE_CXX_COMPILER=$GXX"
        "-DCMAKE_MAKE_PROGRAM=$NINJA"
    )
    info "источник  $src"
    info "сборка    $APP_BUILD_DIR"
    info "Qt        $QT_STATIC_DIR"

    "$CMAKE" "${args[@]}"
}

build_app() {
    step "Собираю приложение"
    "$CMAKE" --build "$APP_BUILD_DIR" --parallel "$JOBS"
}

find_exe() {
    local exe="$APP_BUILD_DIR/MineSweeper_Infinity.exe"
    [ -f "$exe" ] || die "не найден собранный $exe"
    printf '%s\n' "$exe"
}

# ------------------------------------------------------------------
# Проверка результата
# ------------------------------------------------------------------
verify_exe() {
    local exe="$1"
    step 'Проверяю зависимости exe'

    local deps
    deps="$("$OBJDUMP" -p "$exe" | sed -n 's/^[[:space:]]*DLL Name: //p' | sort -u)"

    info "размер: $(du -h "$exe" | cut -f1)"
    info 'подключённые DLL:'
    printf '%s\n' "$deps" | sed 's/^/      /'

    local bad
    bad="$(printf '%s\n' "$deps" | grep -iE '^Qt6|^libstdc|^libgcc|^libwinpthread' || true)"

    if [ -n "$bad" ]; then
        printf '\n'
        warn 'эти DLL придётся класть рядом с exe — статическая линковка неполная:'
        printf '%s\n' "$bad" | sed 's/^/      /'
        return 1
    fi
    info 'Qt и рантайм MinGW вкомпилированы — рядом с exe ничего не нужно'
}

# ------------------------------------------------------------------
# main
# ------------------------------------------------------------------
check_toolchain
ensure_junction

if is_static_qt "$QT_STATIC_DIR"; then
    QT_STATIC_DIR="${QT_STATIC_DIR%/}"
    step "Найден статический Qt: $QT_STATIC_DIR"
else
    found="$(find_static_qt || true)"
    if [ -n "$found" ]; then
        QT_STATIC_DIR="$found"
        step "Найден статический Qt: $QT_STATIC_DIR"
    else
        if [ "$BUILD_QT" -eq 1 ]; then
            build_static_qt
        else
            die "статический Qt не найден (искал в том числе $QT_STATIC_DIR).
Динамический Qt для статической сборки не подходит — нужен Qt, собранный с -static.
Соберите его командой:
  scripts/build-static.sh --build-qt
или укажите готовый каталог:
  scripts/build-static.sh --qt-static-dir=<путь>"
        fi
    fi
fi

if [ "$QT_ONLY" -eq 1 ]; then
    step 'Собран только Qt — приложение не собиралось'
    exit 0
fi

if [ "$CLEAN" -eq 1 ] && [ -d "$APP_BUILD_DIR" ]; then
    step "Удаляю $APP_BUILD_DIR"
    rm -rf "$APP_BUILD_DIR"
fi

configure_app
build_app

EXE="$(find_exe)"
step "Готово: $EXE"

if [ -z "${SKIP_VERIFY:-}" ]; then
    verify_exe "$EXE"
fi

if [ "$RUN_APP" -eq 1 ]; then
    step 'Запускаю'
    warn 'игра пишет миры в AppData — после проверки стоит убрать за собой'
    "$EXE" &
fi
