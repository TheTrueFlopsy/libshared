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
#include <sstream>
#include <stack>
#include <utf8.h>

template <typename T>
static void put_or_append(
  std::vector<T>& v, std::vector<T>::size_type i, const std::vector<T>::value_type& x,
  const std::vector<T>::value_type& pad = T{})
{
  std::vector<T>::size_type n = v.size ();
  
  if (i < n)
    v[i] = x;
  else {
    while (i > n) {
      v.push_back (pad);
      n++;
    }
    v.push_back (x);
  }
}

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
  // The strings are broken into a vector of int, for UTF8 support.
  std::vector <int> characters;
  std::vector <int> layer_numbers;
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
    int character;
    int count = 0;
    while ((character = utf8_next_char (text, cursor)))
    {
      int ch_width = mk_wcwidth ((wchar_t)character);

      switch (ch_width) {
      case 0:  // zero-width / non-graphic character
        break;  // Skip this character.
      case 1:  // ordinary narrow character
        put_or_append (characters, offset + count, character, ' ');
        put_or_append (layer_numbers, offset + count, layer + 1);
        break;
      case 2:  // graphically wide character
        put_or_append (characters, offset + count, character, ' ');
        put_or_append (layer_numbers, offset + count, layer + 1);
        // NOTE: Add a padding space to the next column. If the final output string includes
        // the wide character inserted in the current column, then that character will cover
        // the next column, too.
        put_or_append (characters, offset + count + 1, ' ');
        put_or_append (layer_numbers, offset + count + 1, layer + 1);
        break;
      default:  // Should not happen.
        // ISSUE: Report character width error?
        return std::string();  // Fail.
      }

      count += ch_width;
    }
  }

  // Now walk the character and layer vectors, emitting every character and
  // every detected layer change.
  std::stringstream out;
  int prev_layer = 0;
  for (unsigned int i = 0; i < characters.size (); ++i)
  {
    int curr_layer = layer_numbers[i];
    int character = characters[i];

    // A change in layer triggers a code emit.
    if (prev_layer != curr_layer)
    {
      if (prev_layer)
        out << std::get <2> (_layers[prev_layer - 1]).end ();

      if (curr_layer)
        out << std::get <2> (_layers[curr_layer - 1]).code ();

      prev_layer = curr_layer;
    }

    // IDEA: Cache the character width to avoid calling mk_wcwidth again.
    if (mk_wcwidth ((wchar_t)character) == 2) {  // graphically wide character
      if (i+1 >= characters.size ())
        character = ' ';  // End of composite, no room for wide character.
      else {
        int next_layer = layer_numbers[i+1];
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
}

/*std::string Composite::str () const
{
  // The strings are broken into a vector of int, for UTF8 support.
  std::vector <int> characters;
  std::vector <int> colors;
  for (unsigned int layer = 0; layer < _layers.size (); ++layer)
  {
    const auto& text   = std::get <0> (_layers[layer]);
    auto offset = std::get <1> (_layers[layer]);
    auto len    = utf8_text_length (text);

    // Make sure the vectors are large enough to support a write operator[].
    if (characters.size () < offset + len)
    {
      characters.resize (offset + len, 32);
      colors.resize     (offset + len, 0);
    }

    // Copy in the layer characters and color indexes.
    std::string::size_type cursor = 0;
    int character;
    int count = 0;
    while ((character = utf8_next_char (text, cursor)))
    {
      characters[offset + count] = character;
      colors    [offset + count] = layer + 1;
      ++count;
    }
  }

  // Now walk the character and color vector, emitting every character and
  // every detected color change.
  std::stringstream out;
  int prev_color = 0;
  for (unsigned int i = 0; i < characters.size (); ++i)
  {
    // A change in color triggers a code emit.
    if (prev_color != colors[i])
    {
      if (prev_color)
        out << std::get <2> (_layers[prev_color - 1]).end ();

      if (colors[i])
        out << std::get <2> (_layers[colors[i] - 1]).code ();
      else
        out << std::get <2> (_layers[prev_color - 1]).end ();

      prev_color = colors[i];
    }

    out << utf8_character (characters[i]);
  }

  // Terminate the color codes, if necessary.
  if (prev_color)
    out << std::get <2> (_layers[prev_color - 1]).end ();

  return out.str ();
}*/

////////////////////////////////////////////////////////////////////////////////
// So the same instance can be reused.
void Composite::clear ()
{
  _layers.clear ();
}

////////////////////////////////////////////////////////////////////////////////
