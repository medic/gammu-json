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

#ifndef __BITFIELD_H__
#define __BITFIELD_H__

/**
 * @name bitfield_t:
 */
typedef struct bitfield {

  uint8_t *data;
  unsigned int n;
  unsigned int total_set;

} bitfield_t;

/**
 * @name bitfield_create:
 */
bitfield_t *bitfield_create(size_t bits);

/**
 * @name bitfield_destroy:
 */
void bitfield_destroy(bitfield_t *bf);

/**
 * @name bitfield_test:
 *   Return true if the (zero-based or one-based) bit `bit` is set.
 *   Returns true on success, false if `bit` is out of range for this
 *   bitfield. You may use either zero-based addressing or one-based
 *   addressing, as long as you remain consistent for each instance
 *   of a bitfield_t.
 */
boolean_t bitfield_test(bitfield_t *bf, size_t bit);

/**
 * @name bitfield_set:
 *   Set the (zero-based or one-based) bit `bit` to one if `value` is
 *   true, otherwise set the bit to zero. Returns true on success, false
 *   if the bit `bit` is out of range for this particular bitfield.  You
 *   may use either zero-based addressing or one-based addressing, as
 *   long as you remain consistent for each instance of a bitfield_t.
 */
boolean_t bitfield_set(bitfield_t *bf, size_t bit, boolean_t value);

/**
 * @name bitfield_set_integer_arguments:
 *   Set the Nth bit of `bf` for every string-encoded integer N in `argv`.
 */
boolean_t bitfield_set_integer_arguments(bitfield_t *bf, char *argv[]);

#endif /* __BITFIELD_H__ */

/* vim: set ts=4 sts=2 sw=2 expandtab: */
