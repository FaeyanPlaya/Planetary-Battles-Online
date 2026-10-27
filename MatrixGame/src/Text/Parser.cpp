#include <Text/Parser.hpp>
#include <stupid_logger.hpp>
#include <utils.hpp>

#include <d3dx9core.h>

#include <algorithm>
#include <cwctype>

namespace Text {

bool icase_starts_with(std::wstring_view text, std::wstring_view prefix)
{
    if (text.length() < prefix.length())
    {
        return false;
    }

    for (size_t i = 0; i < prefix.length(); ++i)
    {
        if (towlower(text[i]) != towlower(prefix[i]))
        {
            return false;
        }
    }

    return true;
}

size_t icase_find(std::wstring_view text, std::wstring_view prefix)
{
    auto it =
        std::search(
            text.begin(), text.end(),
            prefix.begin(), prefix.end(),
            [](auto a, auto b) { return towlower(a) == towlower(b); }
        );

    if (it == text.end())
    {
        return std::wstring::npos;
    }

    return std::distance(text.begin(), it);
}

namespace {
bool parse_channel(std::wstring_view text, uint32_t &value) {
    bool negative = false;
    if (!text.empty() && (text.front() == L'+' || text.front() == L'-')) {
        negative = text.front() == L'-';
        text.remove_prefix(1);
    }
    if (text.empty()) return false;
    value = 0;
    for (wchar_t character : text) {
        if (character < L'0' || character > L'9') return false;
        value = value * 10 + (character - L'0');
        if (value > 255) return false;
    }
    return !negative || value == 0;
}
}

D3DCOLOR GetColorFromTag(std::wstring_view text, D3DCOLOR defaultColor)
{
    if (!icase_starts_with(text, COLOR_TAG_START)) return defaultColor;
    const auto end = text.find(L'>');
    if (end == text.npos) return defaultColor;
    const auto values = text.substr(COLOR_TAG_START.size(), end - COLOR_TAG_START.size());
    const auto first = values.find(L',');
    const auto second = first == values.npos ? values.npos : values.find(L',', first + 1);
    uint32_t r, g, b;
    if (first != values.npos && second != values.npos && values.find(L',', second + 1) == values.npos &&
        parse_channel(values.substr(0, first), r) &&
        parse_channel(values.substr(first + 1, second - first - 1), g) &&
        parse_channel(values.substr(second + 1), b)) {
        return 0xff000000u | (r << 16) | (g << 8) | b;
    }
    lgr.error("Failed to parse color from text: {}")(utils::from_wstring(text));
    return defaultColor;
}

std::vector<Token> parse_tokens(std::wstring_view str, Font& font)
{
    std::vector<Token> result;

    const auto processWord = [&result](std::wstring_view word) {
        while (true) {
            size_t pos = icase_find(word, COLOR_TAG_START);
            if (pos == word.npos) {
                result.emplace_back(word);
                return;
            }
            if (pos != 0) {
                result.emplace_back(word.substr(0, pos));
                word.remove_prefix(pos);
            }
            pos = icase_find(word, COLOR_TAG_END);
            if (pos == word.npos) {
                result.emplace_back(word);
                return;
            }
            result.emplace_back(word.substr(0, pos + COLOR_TAG_END.size()));
            word.remove_prefix(pos + COLOR_TAG_END.size());
            if (word.empty()) return;
        }
    };

    size_t pos = 0;
    while((pos = str.find_first_of(L" \r\n"), pos) != std::wstring::npos)
    {
        if (str[pos] == L' ') // space - just split words
        {
            processWord(str.substr(0, pos));
            result.emplace_back(L" ");
            str.remove_prefix(pos + 1);
        }
        else // CRLF, lone CR and LF all produce one explicit line break
        {
            processWord(str.substr(0, pos));
            result.emplace_back(L"\r\n");
            const size_t length = str[pos] == L'\r' && pos + 1 < str.size() && str[pos + 1] == L'\n' ? 2 : 1;
            str.remove_prefix(pos + length);
        }
    }

    processWord(str);

    bool in_color_tag = false;
    uint32_t color = 0;
    for (auto& token : result)
    {
        if (token.text == L" " || token.text == L"\r\n")
        {
            continue;
        }

        std::wstring_view text = token.text;

        if (!in_color_tag)
        {
            color = token.color;
        }

        const auto openingEnd = text.find(L'>');
        if (icase_starts_with(text, COLOR_TAG_START) && openingEnd != text.npos)
        {
            in_color_tag = true;
            color = GetColorFromTag(text, token.color);
            text.remove_prefix(openingEnd + 1);
        }

        auto pos = icase_find(text, COLOR_TAG_END);
        if (pos != std::wstring::npos)
        {
            in_color_tag = false;
            text.remove_suffix(text.length() - pos);
        }

        token.text  = text;
        token.color = color;
        token.width = font.CalcTextWidth(text);
    }

    return result;
}

size_t calc_lines(const std::vector<Token>& text, Font& font, const RECT &rect)
{
    const size_t line_width = rect.right > rect.left ? static_cast<size_t>(int64_t(rect.right) - rect.left) : 0;

    size_t lines = 1;
    size_t cur_width = 0;
    for (auto& token : text)
    {
        if (token.text == L"\r\n")
        {
            cur_width = 0;
            lines++;
        }
        else if (token.text == L" ")
        {
            if (cur_width != 0) // if not a new line
            {
                cur_width += font.GetSpaceWidth();
            }
        }
        else
        {
            if (cur_width == 0 || (cur_width <= line_width && token.width <= line_width - cur_width))
            {
                cur_width += token.width;
            }
            else
            {
                cur_width = token.width;
                lines++;
            }
        }
    }

    return lines;
}

} // namespace Text
