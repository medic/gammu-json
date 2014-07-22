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

#define _GNU_SOURCE

#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>
#include <inttypes.h>

#include "allocate.h"
#include "bitfield.h"

/** --- **/

#define bitfield_cell_width (CHAR_BIT)

/** --- **/

/**
 * @name bitfield_create:
 */
bitfield_t *bitfield_create(size_t bits) {

  size_t size = bits + 1; /* One-based */
  size_t cells = (size / bitfield_cell_width);

  bitfield_t *rv = allocate(sizeof(*rv));

  if (size % bitfield_cell_width) {
    cells++;
  }

  rv->n = bits;
  rv->total_set = 0;
  rv->data = allocate_array(bitfield_cell_width, cells, 0);

  return rv;
}

/**
 * @name bitfield_destroy:
 */
void bitfield_destroy(bitfield_t *bf) {

  free(bf->data);
  free(bf);
}

/**
 * @name bitfield_test:
 *   Return true if the (zero-based or one-based) bit `bit` is set.
 *   Returns true on success, false if `bit` is out of range for this
 *   bitfield. You may use either zero-based addressing or one-based
 *   addressing, as long as you remain consistent for each instance
 *   of a bitfield_t.
 */
boolean_t bitfield_test(bitfield_t *bf, size_t bit) {

  if (bit > bf->n) {
    return FALSE;
  }

  size_t cell = (bit / bitfield_cell_width);
  size_t offset = (bit % bitfield_cell_width);

  return (bf->data[cell] & (1 << offset));
}

/**
 * @name bitfield_set:
 *   Set the (zero-based or one-based) bit `bit` to one if `value` is
 *   true, otherwise set the bit to zero. Returns true on success, false
 *   if the bit `bit` is out of range for this particular bitfield. You
 *   may use either zero-based addressing or one-based addressing, as
 *   long as you remain consistent for each instance of a bitfield_t.
 */
boolean_t bitfield_set(bitfield_t *bf, size_t bit, boolean_t value) {

  if (bit > bf->n) {
    return FALSE;
  }

  size_t cell = (bit / bitfield_cell_width);
  size_t offset = (bit % bitfield_cell_width);

  unsigned int prev_value = (
    (bf->data[cell] & (1 << offset)) != 0
  );

  if (value) {
    bf->data[cell] |= (1 << offset);
  } else {
    bf->data[cell] &= ~(1 << offset);
  }

  if (value && !prev_value) {
    bf->total_set++;
  } else if (prev_value && !value) {
    bf->total_set--;
  }

  return TRUE;
}

/**
 * @name bitfield_set_integer_arguments:
 */
boolean_t bitfield_set_integer_arguments(bitfield_t *bf, char *argv[]) {

  for (size_t i = 0; argv[i] != NULL; i++) {

    char *err = NULL;
    size_t n = strtoul(argv[i], &err, 10);

    if (err == NULL || *err != '\0') {
      return FALSE;
    }

    if (!bitfield_set(bf, n, TRUE)) {
      return FALSE;
    }
  }

  return TRUE;
}

/* vim: set ts=4 sts=2 sw=2 expandtab: */
