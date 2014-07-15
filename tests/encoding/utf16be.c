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
}

/**
 * @name test_string_info:
 */
void test_string_info() {

  const char *s = NULL;

  /* U+1F62C Grimacing face, U+1F610 Neutral face */
  s = "\xd8\x3d\xde\x2c\xd8\x3d\xde\x10\0\0";

  string_info_assert(
    s, (string_info_t) { 8, 4, 2, D_ERR_NONE, 0 }
  );

  /* Invalid surrogate: missing trailing */
  s = "\xd8";

  string_info_assert(
    s, (string_info_t) { 1, 0, 0, D_ERR_PARTIAL_UNIT, 0 }
  );
}

/**
 * @name main:
 */
int main(int argc, char *argv[]) {

  test_string_info();
}

/* vim: set ts=4 sts=2 sw=2 expandtab: */
