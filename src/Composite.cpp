////////////////////////////////////////////////////////////////////////////////
//
// Copyright 2016 - 2021, 2023, Gothenburg Bit Factory.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// https://opensource.org/license/mit
//
////////////////////////////////////////////////////////////////////////////////

#include <Composite.h>
#include <limits>
#include <sstream>
#include <stack>
#include <utf8.h>


////////////////////////////////////////////////////////////////////////////////

namespace {

  // Helper function that either replaces a pre-existing element at index (i) in
  // a std::vector with the value (x) (if (i) is less than the size of the vector)
  // or extends the vector in such a way that it ends up with (i+1) elements, with
  // the value (x) at index (i) and the padding value (pad) at each index between
  // that of the final pre-existing element of the vector and (i).
  template <typename T>
  void put_or_extend (
    std::vector<T>& v, typename std::vector<T>::size_type i, const T& x, const T& pad = T{})
  {
    if (i < v.size ())
      v[i] = x;
    else
    {
      v.resize (i, pad);
      v.push_back (x);
    }
  }

  struct ColumnData
  {
    unsigned int layer_num;

    std::string::size_type text_begin_i;

    std::string::size_type text_end_i;

    unsigned char char_0_width;

    ColumnData (
      unsigned int layer=0, std::string::size_type begin_i=1, std::string::size_type end_i=0,
      unsigned char c_0_w=0)
    :
      layer_num (layer), text_begin_i (begin_i), text_end_i (end_i), char_0_width (c_0_w)
    {}

    ColumnData (const ColumnData& orig) = default;

    ColumnData &operator= (const ColumnData& orig) = default;

    std::string::difference_type char_count () const
    {
      return text_end_i - text_start_i;
    }

    void make_padding ()
    {
      text_begin_i = 1;
      text_end_i = 0;
      char_0_width = 0;
    }

    bool is_padding () const
    {
      return char_count () < 0;
    }
  };

  const ColumnData LAYER_0_PAD ();

  const std::string::size_type INVALID_COLUMN_I = std::numeric_limits<std::string::size_type>::max ();

  inline void do_halfcovered_wide_char_check (
    std::vector<ColumnData>& columns, std::vector<ColumnData>::size_type column_i)
  {
    // If there is a wide character (on a lower layer) in the preceding column, replace
    // that character (and any nonspacing characters associated with it) with padding.
    // (Because the second half of that character will be covered, and we couldn't display
    // half a character if we wanted to.)
    if (column_i >= 1 && column_i-1 < columns.size ())
    {
      ColumnData &prev_col_data = columns[column_i-1];
      if (prev_col_data.char_0_width == 2)
        prev_col_data.make_padding ();
    }
  }

};

////////////////////////////////////////////////////////////////////////////////
// Initially assume no text, but infinite virtual space.
//
// Allow overlay placement of arbitrary text at any offset, real or virtual, and
// using a specific color.
//
// For example:
//   Composite c;
//   c.add ("aaaaaaaaaa",  2, Color ("..."));    // Layer 1
//   c.add ("bbbbb",       5, Color ("..."));    // Layer 2
//   c.add ("c",          15, Color ("..."));    // Layer 3
//
//   _layers = { std::make_tuple ("aaaaaaaaaa",  2, Color ("...")),
//               std::make_tuple ("bbbbb",       5, Color ("...")),
//               std::make_tuple ("c",          15, Color ("..."))};
//
void Composite::add (
  const std::string& text,
  std::string::size_type offset,
  const Color& color)
{
  _layers.emplace_back (text, offset, color);
}

////////////////////////////////////////////////////////////////////////////////
// Merge the layers of text and color into one string.
//
// For example:
//   Composite c;
//   c.add ("aaaaaaaaaa",  2, Color ("..."));    // Layer 1
//   c.add ("bbbbb",       5, Color ("..."));    // Layer 2
//   c.add ("c",          15, Color ("..."));    // Layer 3
//
//   _layers = { std::make_tuple ("aaaaaaaaaa",  2, Color ("...")),
//               std::make_tuple ("bbbbb",       5, Color ("...")),
//               std::make_tuple ("c",          15, Color ("..."))};
//
// Arrange strings conceptually:
//              111111
//    0123456789012345     // Position
//
//      aaaaaaaaaa         // Layer 1
//         bbbbb           // Layer 2
//                   c     // Layer 3
//
// Walk all strings left to right, selecting the character and color from the
// highest numbered layer. Emit color codes only on edge detection.
//
std::string Composite::str () const
{
  std::vector <ColumnData> columns;

  // Determine the content of each column in the composited string. Apply layers in sequence.
  for (unsigned int layer_i = 0; layer_i < _layers.size (); ++layer_i)
  {
    const auto& text = std::get <0> (_layers[layer_i]);
    auto offset = std::get <1> (_layers[layer_i]);
    auto len = utf8_text_length (text);

    // Make sure the column vector is large enough to support push_back() without reallocation.
    if (columns.capacity () < offset + len)
      columns.reserve (offset + len);

    // Inspect and decide how to handle each character (i.e. Unicode code point)
    // in the current layer text.
    std::string::size_type prev_cursor = 0;
    std::string::size_type cursor = 0;
    unsigned int column_count = 0;
    std::string::size_type prev_spacer_column_i = INVALID_COLUMN_I;
    unsigned int character;
    while ((character = utf8_next_char (text, cursor)))
    {
      std::string::size_type column_i = offset + column_count;
      int ch_width = mk_wcwidth ((wchar_t)character);

      switch (ch_width)
      {
      case 0:  // zero-width / non-graphic character
        if (prev_spacer_column_i == INVALID_COLUMN_I)  // No preceding spacing character on this layer.
          ;  // Skip this character.
        else  // There is a preceding spacing character on this layer.
        {
          // Append the nonspacing character to the column of the previous spacing character.
          columns[prev_spacer_column_i].text_end_i = cursor;
        }
        break;
      case 1:  // ordinary narrow character
        if (prev_spacer_column_i == INVALID_COLUMN_I)
          do_halfcovered_wide_char_check (columns, column_i);

        // Put the character in the appropriate column. Pad out the column list as necessary.
        put_or_extend (columns, column_i, ColumnData (layer_i+1, prev_cursor, cursor, 1), LAYER_0_PAD);

        prev_spacer_column_i = column_i;
        column_count += 1;
        break;
      case 2:  // graphically wide character
        if (prev_spacer_column_i == INVALID_COLUMN_I)
          do_halfcovered_wide_char_check (columns, column_i);

        // Put the character in the appropriate column. Pad out the column list as necessary.
        // Make the column after the current one (which is also covered by the wide character)
        // a pad column on the current layer.
        put_or_extend (columns, column_i, ColumnData (layer_i+1, prev_cursor, cursor, 2), LAYER_0_PAD);
        put_or_extend (columns, column_i+1, ColumnData (layer_i+1), LAYER_0_PAD);

        prev_spacer_column_i = column_i;
        column_count += 2;
        break;
      default:  // Should not happen.
        // ISSUE: Report character width error?
        return std::string ();  // Fail.
      }

      prev_cursor = cursor;
    }
  }

  // Now walk the column vector, emitting every character and every detected layer change.
  std::stringstream out;
  unsigned int prev_layer = 0;
  for (unsigned int column_i = 0; column_i < columns.size (); ++column_i)
  {
    auto column_data = columns[i];
    auto curr_layer = column_data.layer_num;
    const auto& text = std::get <0> (_layers[curr_layer-1]);

    // A change in layer_i triggers a code emit.
    if (prev_layer != curr_layer)
    {
      if (prev_layer)
        out << std::get <2> (_layers[prev_layer-1]).end ();

      if (curr_layer)
        out << std::get <2> (_layers[curr_layer-1]).code ();

      prev_layer = curr_layer;
    }

    if (column_data.is_padding ())
      out << ' ';
    else
      out << std::string (text, column_data.text_begin_i, column_data.char_count ());

    if (column_data.char_0_width == 2)
      ++column_i;
  }

  // Terminate the color codes, if necessary.
  if (prev_layer)
    out << std::get <2> (_layers[prev_layer-1]).end ();

  return out.str ();
}

/*std::string Composite::str () const
{
  // The strings are broken into a vector of unsigned int, for UTF-8 support.
  std::vector <unsigned int> characters;
  std::vector <unsigned int> layer_numbers;
  for (unsigned int layer = 0; layer < _layers.size (); ++layer)
  {
    const auto& text = std::get <0> (_layers[layer]);
    auto offset = std::get <1> (_layers[layer]);
    auto len = utf8_text_length (text);

    // Make sure the vectors are large enough to support push_back() without reallocation.
    if (characters.capacity () < offset + len)
    {
      characters.reserve (offset + len);
      layer_numbers.reserve (offset + len);
    }

    // Copy in the layer characters and layer numbers.
    std::string::size_type cursor = 0;
    unsigned int character;
    unsigned int count = 0;
    while ((character = utf8_next_char (text, cursor)))
    {
      std::string::size_type ch_column = offset + count;
      int ch_width = mk_wcwidth ((wchar_t)character);

      switch (ch_width)
      {
      case 0:  // zero-width / non-graphic character
        break;  // Skip this character.
      case 1:  // ordinary narrow character
        put_or_extend (characters, ch_column, character, (unsigned int)' ');
        put_or_extend (layer_numbers, ch_column, layer + 1);
        break;
      case 2:  // graphically wide character
        put_or_extend (characters, ch_column, character, (unsigned int)' ');
        put_or_extend (layer_numbers, ch_column, layer + 1);
        // NOTE: Put a padding space in the next column. If the final output string includes
        // the wide character inserted in the current column, then that character will cover
        // the next column, too.
        put_or_extend (characters, ch_column + 1, (unsigned int)' ');
        put_or_extend (layer_numbers, ch_column + 1, layer + 1);
        break;
      default:  // Should not happen.
        // ISSUE: Report character width error?
        return std::string ();  // Fail.
      }

      count += (unsigned int)ch_width;  // If we get here, ch_width is in { 0, 1, 2 }.
    }
  }

  // Now walk the character and layer vectors, emitting every character and
  // every detected layer change.
  std::stringstream out;
  unsigned int prev_layer = 0;
  for (unsigned int i = 0; i < characters.size (); ++i)
  {
    unsigned int curr_layer = layer_numbers[i];
    unsigned int character = characters[i];

    // A change in layer triggers a code emit.
    if (prev_layer != curr_layer)
    {
      // IDEA: Suppress code emission if prev_layer and curr_layer have equivalent Colors.
      if (prev_layer)
        out << std::get <2> (_layers[prev_layer - 1]).end ();

      if (curr_layer)
        out << std::get <2> (_layers[curr_layer - 1]).code ();

      prev_layer = curr_layer;
    }

    // IDEA: Cache the character width to avoid calling mk_wcwidth again.
    if (mk_wcwidth ((wchar_t)character) == 2)  // graphically wide character
    {
      if (i+1 >= characters.size ())  // Won't happen if every wide char is followed by a padding space.
        character = ' ';  // End of composite, no room for wide character.
      else
      {
        unsigned int next_layer = layer_numbers[i+1];
        if (curr_layer != next_layer)
          character = ' ';  // Layer change at next column, no room for wide character.
        else
          ++i;  // Wide character will be emitted, skip next column.
      }
    }

    out << utf8_character (character);
  }

  // Terminate the color codes, if necessary.
  if (prev_layer)
    out << std::get <2> (_layers[prev_layer - 1]).end ();

  return out.str ();
}*/

////////////////////////////////////////////////////////////////////////////////
// So the same instance can be reused.
void Composite::clear ()
{
  _layers.clear ();
}

////////////////////////////////////////////////////////////////////////////////
