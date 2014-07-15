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

#include <string.h>
#include "allocate.h"

/**
 * @name addition_will_overflow:
 */
int addition_will_overflow(size_t a, size_t b) {

  return (((size_t) -1 - a) < b);
}

/**
 * @name multiplication_will_overflow:
 */
int multiplication_will_overflow(size_t a, size_t b) {

  if (a == 0 || b == 0) {
    return FALSE;
  }

  return (((size_t) -1 / b) < a);
}

/**
 * @name allocate:
 */
void *allocate(size_t size) {

  void *rv = malloc(size);

  if (!rv) {
    fatal(127, "allocation failure; couldn't allocate %lu bytes", size);
  }

  memset(rv, '\0', size);
  return rv;
}

/**
 * @name allocate_array:
 */
void *allocate_array(size_t size, size_t items, size_t extra) {

  const char *multiplication_message =
    "allocation failure; multiplication would overflow (%lu * %lu)";

  const char *addition_message =
    "allocation failure; addition would overflow (%lu + %lu)";

  if (multiplication_will_overflow(size, items)) {
    fatal(126, multiplication_message, size, items);
  }
  
  if (multiplication_will_overflow(size, extra)) {
    fatal(125, multiplication_message, size, extra);
  }

  items *= size;
  extra *= size;

  if (addition_will_overflow(items, extra)) {
    fatal(124, addition_message, items, extra);
  }

  return allocate(items + extra);
}

/**
 * @name reallocate:
 */
void *reallocate(void *p, size_t size) {

  void *rv = realloc(p, size);

  if (!rv) {
    warn("reallocation failure; couldn't enlarge region to %lu bytes", size);
    return NULL;
  }

  return rv;
}

/**
 * @name reallocate_array:
 */
void *reallocate_array(void *p, size_t size, size_t items, size_t extra) {

  const char *multiplication_message =
    "reallocation failure; multiplication would overflow (%lu * %lu)";

  const char *addition_message =
    "reallocation failure; addition would overflow (%lu * %lu)";

  if (multiplication_will_overflow(size, items)) {
    warn(multiplication_message, size, items);
    return NULL;
  }

  if (multiplication_will_overflow(size, extra)) {
    warn(multiplication_message, size, extra);
    return NULL;
  }

  items *= size;
  extra *= size;

  if (addition_will_overflow(items, extra)) {
    warn(addition_message, items, extra);
    return NULL;
  }

  return reallocate(p, items + extra);
}

/* vim: set ts=4 sts=2 sw=2 expandtab: */
