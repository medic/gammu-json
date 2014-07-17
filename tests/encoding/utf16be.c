/**
 * gammu-json
 *
 * Copyright (c) 2013-2014 David Brown <hello at scri.pt>.
 * Copyright (c) 2013-2014 Medic Mobile, Inc. <david at medicmobile.org>
 *
 * All rights reserved.
 *
 * This is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License, version three,
 * as published by the Free Software Foundation.
 *
 * You should have received a copy of version three of the GNU General
 * Public License along with this software. If you did not, see
 * http://www.gnu.org/licenses/.
 *
 * This software is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL DAVID BROWN OR
 * MEDIC MOBILE BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <assert.h>
#include "encoding.h"

/**
 * @name string_info_assert:
 */
void string_info_assert(const char *s, string_info_t expect) {

  string_info_t si;
  utf16be_string_info(s, &si);

  assert(si.bytes == expect.bytes);
  assert(si.units == expect.units);
  assert(si.symbols == expect.symbols);
  assert(si.error == expect.error);
  assert(si.error_offset == expect.error_offset);
  assert(si.invalid_bytes == expect.invalid_bytes);
}

/**
 * @name test_string_info:
 */
void test_string_info() {

  const char *s = NULL;

  /* U+1F62C: Grimacing Face, U+1F610: Neutral Face */
  s = "\xd8\x3d\xde\x2c\xd8\x3d\xde\x10\0\0";

  string_info_assert(
    s, (string_info_t) {
      .error = D_ERR_NONE, .error_offset = 0, 
        .bytes = 8, .units = 4, .symbols = 2, .invalid_bytes = 0
    }
  );

  /* Missing trailing surrogate */
  s = "\xd8\0\0\0";

  string_info_assert(
    s, (string_info_t) {
      .error = D_ERR_UNMATCHED_SURROGATE, .error_offset = 0,
        .bytes = 2, .units = 1, .symbols = 0, .invalid_bytes = 2
    }
  );

  /* Unexpected lead surrogate */
  s = "\xdf\x00\0\0";

  string_info_assert(
    s, (string_info_t) {
      .error = D_ERR_UNEXPECTED_SURROGATE, .error_offset = 0,
        .bytes = 2, .units = 1, .symbols = 0, .invalid_bytes = 2
    }
  );

  /* Missing trailing surrogate */
  s = "\xd8\x3d\xde\x2c\xd9\x00\xd9\x01\xd8\x3d\xde\x10\0\0";

  string_info_assert(
    s, (string_info_t) {
      .error = D_ERR_UNMATCHED_SURROGATE, .error_offset = 4,
        .bytes = 12, .units = 6, .symbols = 2, .invalid_bytes = 4
    }
  );

  /* U+0020: ASCII Space */
  s = "\x00\x20\0\0";

  string_info_assert(
    s, (string_info_t) {
      .error = D_ERR_NONE, .error_offset = 0,
        .bytes = 2, .units = 1, .symbols = 1, .invalid_bytes = 0
    }
  );

  /* U+0020: Whitespace, trailing surrogate */
  s = "\x00\x20\xdc\x00\0\0";

  string_info_assert(
    s, (string_info_t) {
      .error = D_ERR_UNEXPECTED_SURROGATE, .error_offset = 2,
        .bytes = 4, .units = 2, .symbols = 1, .invalid_bytes = 2
    }
  );

  /* Missing trailing surrogate, uneven */
  s = "\xd8\x3d\xde\x2c\xd9\x00\xd8\x3d\xde\x10\0\0\0";

  string_info_assert(
    s, (string_info_t) {
      .error = D_ERR_UNMATCHED_SURROGATE, .error_offset = 4,
        .bytes = 10, .units = 5, .symbols = 2, .invalid_bytes = 2
    }
  );

  /* Garbage, U+1F62C: Grimacing Face */
  s = "\xdf\xdc\xdf\xff\xd8\x00\xd8\x3d\xde\x2c\0\0";

  string_info_assert(
    s, (string_info_t) {
      .error = D_ERR_UNEXPECTED_SURROGATE, .error_offset = 0, 
        .bytes = 10, .units = 5, .symbols = 1, .invalid_bytes = 6
    }
  );

  /* Two lead surrogates, U+1F62C: Grimacing Face */
  s = "\xd8\x00\xd8\x00\xd8\x3d\xde\x2c\0\0";

  string_info_assert(
    s, (string_info_t) {
      .error = D_ERR_UNMATCHED_SURROGATE, .error_offset = 0, 
        .bytes = 8, .units = 4, .symbols = 1, .invalid_bytes = 4
    }
  );

}

/**
 * @name main:
 */
int main(int argc, char *argv[]) {

  test_string_info();
}

/* vim: set ts=4 sts=2 sw=2 expandtab: */
