# テスト実行可能ファイルを定義
add_executable(runUnitTests
    mock_test.cpp
)

# テスト対象のライブラリ (MyLib) と Google Test/Mock にリンク
target_link_libraries(runUnitTests PRIVATE
    sr_arq           # 自作ライブラリにリンク
    GTest::gtest_main # gtestのmain関数を含むライブラリ (main関数を自分で書かない場合)
    GTest::gmock_main # gmockを使う場合はこちらもリンク (gtest_mainに含まれる場合もある)
)

# CTestにテストを追加 (Google Test Discover Tests)
# これにより、実行ファイル内の全テストケースを自動検出してくれる
include(GoogleTest)
gtest_discover_tests(runUnitTests)