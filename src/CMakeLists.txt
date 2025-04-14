# ヘッダオンリーライブラリ "sr_arq" の定義
add_library(srarq INTERFACE)

# (任意) このライブラリを構成するヘッダファイルをCMakeに認識させる
#       IDEのプロジェクトビューなどに表示されるようになる
target_sources(srarq INTERFACE
    # ヘッダファイルが src/include/sr_arq ディレクトリにあると仮定
    "${CMAKE_CURRENT_SOURCE_DIR}/srarq.h"
    # "${CMAKE_CURRENT_SOURCE_DIR}/include/sr_arq/your_header2.h"
    # ... 他のヘッダーファイル
    # あるいはディレクトリ丸ごと指定も可能 (ただし非推奨の場合も)
    # FILE_GLOB(SR_ARQ_HEADERS "include/sr_arq/*.h")
    # target_sources(sr_arq INTERFACE ${SR_ARQ_HEADERS})
)

# 最も重要: このライブラリを使うターゲットがヘッダを見つけられるように、
# インクルードディレクトリを指定する
target_include_directories(srarq INTERFACE
    # ヘッダファイルが src/include ディレクトリにあると仮定
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_compile_features(srarq INTERFACE cxx_std_11)