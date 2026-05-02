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


// Helper function that either replaces a pre-existing element at index (i) in
// a std::vector with the value (x) (if (i) is less than the size of the vector)
// or extends the vector in such a way that it ends up with (i+1) elements, with
// the value (x) at index (i) and the padding value (pad) at each index between
// that of the final pre-existing element of the vector and (i).
template <typename T>
static void put_or_extend(
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
        put_or_extend (characters, ch_column, character, ' ');
        put_or_extend (layer_numbers, ch_column, layer + 1);
        break;
      case 2:  // graphically wide character
        put_or_extend (characters, ch_column, character, ' ');
        put_or_extend (layer_numbers, ch_column, layer + 1);
        // NOTE: Put a padding space in the next column. If the final output string includes
        // the wide character inserted in the current column, then that character will cover
        // the next column, too.
        put_or_extend (characters, ch_column + 1, ' ');
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
}

////////////////////////////////////////////////////////////////////////////////
// So the same instance can be reused.
void Composite::clear ()
{
  _layers.clear ();
}

////////////////////////////////////////////////////////////////////////////////
