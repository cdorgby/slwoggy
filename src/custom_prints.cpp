#include <cstring>
#include "slwoggy.hpp"


using namespace slwoggy;
using namespace std::chrono_literals;

LOG_MODULE_NAME("main");

struct test_printer
{
    int a;
};


template <> struct fmt::formatter<test_printer> : formatter<string_view>
{
    // parse is inherited from formatter<string_view>.

    auto format(test_printer c, format_context &ctx) const -> format_context::iterator
    {
        return fmt::format_to(ctx.out(), "{}", c.a);
    }
};

struct format_as_test
{
    int a;
};

auto format_as(const format_as_test &v) -> std::string { return fmt::format("formatted({})", v.a); }

int main(int argc, char *argv[])
{
    auto tp = test_printer{42};
    auto tp2 = test_printer{0x55};

    auto ft = format_as_test{12345};

    LOG(info) << "Custom prints example";
    LOG(info) << tp;
    LOG(debug).printfmt("tp2: {}", tp2);
    LOG(trace).format("ft: {}", ft);

    log_line_dispatcher::instance().flush();

    // Print out metrics from dispatcher and buffer pool

    return 0;
}