#pragma once

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
#include <functional>

namespace tt::string {

/**
 * Given a filesystem path as input, try and get the parent path.
 * @param[in] path input path
 * @param[out] output an output buffer that is allocated to at least the size of "current"
 * @return true when successful
 */
bool getPathParent(const std::string& path, std::string& output);

/**
 * Given a filesystem path as input, get the last segment of a path
 * @param[in] path input path
 */
std::string getLastPathSegment(const std::string& path);

/**
 * Splits the provided input into separate pieces with delimiter as separator text.
 * When the input string is empty, the output list will be empty too.
 *
 * @param input the input to split up
 * @param delimiter a non-empty string to recognize as separator
 */
std::vector<std::string> split(const std::string& input, const std::string& delimiter);

/**
 * Splits the provided input into separate pieces with delimiter as separator text.
 * When the input string is empty, the output list will be empty too.
 *
 * @param input the input to split up
 * @param delimiter a non-empty string to recognize as separator
 * @param callback the callback function that receives the split parts
 */
void split(const std::string& input, const std::string& delimiter, std::function<void(const std::string&)> callback);

/**
 * Join a set of tokens into a single string, given a delimiter (separator).
 * If the input is an empty list, the result will be an empty string.
 * The delimeter is only placed inbetween tokens and not appended at the end of the resulting string.
 *
 * @param input the tokens to join together
 * @param delimiter the separator to join with
 */
std::string join(const std::vector<std::string>& input, const std::string& delimiter);

/**
 * Join a set of tokens into a single string, given a delimiter (separator).
 * If the input is an empty list, the result will be an empty string.
 * The delimeter is only placed inbetween tokens and not appended at the end of the resulting string.
 *
 * @param input the tokens to join together
 * @param delimiter the separator to join with
 */
std::string join(const std::vector<const char*>& input, const std::string& delimiter);

/**
 * Returns the lowercase value of a string.
 * @warning This only works for strings with 1 byte per character
 * @param[in] the string with lower and/or uppercase characters
 * @return a string with only lowercase characters
 */
template <typename T>
std::basic_string<T> lowercase(const std::basic_string<T>& input) {
    std::basic_string<T> output = input;
    std::transform(
        output.begin(),
        output.end(),
        output.begin(),
        [](const T character) { return static_cast<T>(std::tolower(character)); }
    );
    return std::move(output);
}

/** @return true when input only has hex characters: [a-z], [A-Z], [0-9] */
bool isAsciiHexString(const std::string& input);

/** @return the first part of a file name right up (and excluding) the first period character. */
std::string removeFileExtension(const std::string& input);

/**
 * Remove the given characters from the start and end of the specified string.
 * @param[in] input the text to trim
 * @param[in] characters the characters to remove from the input
 * @return the input where the specified characters are removed from the start and end of the input string
 */
std::string trim(const std::string& input, const std::string& characters);

} // namespace
