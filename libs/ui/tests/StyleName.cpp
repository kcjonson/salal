// Built twice (libs/ui/CMakeLists.txt): as is, it compiles and exits 0; with SALAL_STYLE_TYPO it names
// a style that doesn't exist and must fail to compile.

#include <ui/theme/Tokens.h>

#ifdef SALAL_STYLE_TYPO
constexpr ui::Style kStyle = ui::styles::pnae;
#else
constexpr ui::Style kStyle = ui::styles::pane;
#endif

int main() {
	return kStyle.theme.table == &ui::tokenTable() ? 0 : 1;
}
