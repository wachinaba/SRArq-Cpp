cmake_minimum_required(VERSION 3.14) # FetchContentには3.11以上、gtestには3.14あたりを推奨
project(srarq LANGUAGES CXX)

# C++標準バージョンの指定 (例: C++17)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) # 標準に準拠する場合はOFF推奨

# --- Google Test の FetchContent 設定 ---
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG    v1.14.0 # 特定のバージョンを指定 (最新安定版を確認推奨)
)
# gtest と gmock のテストをビルドしないように設定 (必須ではないが推奨)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE) # WindowsのMSVCランタイム関連で必要になることが多い
set(BUILD_GMOCK ON CACHE BOOL "Build gMock")       # gmockも使うのでONに (デフォルトONのはず)
set(INSTALL_GTEST OFF CACHE BOOL "Disable installing GTest") # インストールは不要

FetchContent_MakeAvailable(googletest)
# ----------------------------------------

# ライブラリのサブディレクトリを追加
add_subdirectory(src)

# --- テストの設定 ---
enable_testing() # CTestを有効化

# testsサブディレクトリを追加 (googletestの後に追加するのが一般的)
# googletestが利用可能になってからテストコードをビルドするため
add_subdirectory(tests)
# --------------------