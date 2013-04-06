
var path = require('path'),
    async = require('async'),
    _ = require('underscore'),
    jsdump = require('jsDump'),
    moment = require('moment'),
    child = require('child_process');

/**
 * @namespace node-gammu-json:
 */
exports.prototype = {

    /**
     * @name _setenv:
     *   Set environment variable using a callback.
     */
    _setenv: function (_key, _callback) {

      process.env[_key] = _callback(process.env[_key])
    },

    /**
     * @name _start_polling:
     */
    _start_polling: function () {
      
      this._is_polling = true;
      this._handle_polling_timeout();

      return this;
    },

    /**
     * @name _stop_polling:
     */
    _stop_polling: function () {

      this._is_polling = false;
    },


    /**
     * @name _handle_polling_timeout:
     */
    _handle_polling_timeout: function () {

      var self = this;

      /* Check for termination:
          This is set via the `_stop_polling` method, above. */

      if (!self._is_polling) {
        return false;
      }

      /* Queue processing:
          Send messages, receive messages, then schedule next run. */

      async.waterfall([

        function (_next_fn) {
          self._send_messages(_next_fn);
        },

        function (_next_fn) {
          self._receive_messages(_next_fn);
        }

      ], function (_err) {

        if (self._is_polling) {
          setTimeout(
            _.bind(self._handle_polling_timeout, self),
              self._poll_interval
          );
        }
      });

      return true;
    },

    /**
     * @name _create_message_transmission_args:
     *   Return an array of interleaved phone numbers and message bodies,
     *   suitable for transmission using the `gammu-json` `send` command.
     *   These are command-line arguments for now, but might be sent via
     *   `stdin` instead once `gammu-json` actually supports it.
     */
    _create_message_transmission_args: function (_messages) {

      return [];
    },

    /**
     * @name _send_messages:
     */
    _send_messages: function (_callback) {

      var self = this;

      if (self._outbound_queue.length <= 0) {
        return _callback();
      }

      var args = [ 'send' ].concat(
        self._create_message_transmission_args(self._outbound_queue)
      );

      if (args.length <= 0) {
        return _callback();
      }

      self._subprocess('gammu-json', args, function (_err, _rv) {

        if (_err) {
          return _callback(_err);
        }

        self._deliver_transmit_results(_rv, _callback);
      });
    },

    /**
     * @name _deliver_transmit_results:
     */
    _deliver_transmit_results: function (_callback) {

      var self = this;
      var unsent_messages = [];

      async.each(_rv,

        function (_r, _next_fn) {

          /* Map result back to message:
              The `create_message_transmission_args` function guarantees
              that it will process the outbound queue in order, from offset
              zero onward. Because of this, the (one-based) index of the
              transmission result object will always imply the (zero-based)
              index of its corresponding message object in the outgoing queue. */
              
          var queue_index = _result.index - 1;
          var message = self._outbound_queue[queue_index];

          if (_r.result != 'success') {
            unsent_messages.push(_message);
            return _next_fn();
          }

          /* Notify client of successful transmission:
              This is different than the receive case, since we can't
              unsend the already-transmitted message. Thus, there's no
              possible error to check here, and we just continue on. */

          self._notify_transmit(message, _result, function (_error) {
            return _next_fn();
          });
        },

        function (_err) {

          /* Finish up:
              Replace the outbound queue with the messages we didn't
              transmit successfully; the next queue run will retry. */

          self._outbound_queue = unsent_messages;
          return _callback();
        }
      );
    },

    /** @name _receive_messages:
     */
    _receive_messages: function (_callback) {

      var self = this;

      self._subprocess('gammu-json', [ 'retrieve' ], function (_err, _rv) {

        if (_err) {
          return _callback(_err);
        }

        for (var i = 0, len = _rv.length; i < len; ++i) {

          if (_rv[i].total_parts <= 1) {
            self._inbound_queue.push(_rv[i]);
          } else {
            self._reassemble_message(_rv[i]);
          }
        }

        self._deliver_incoming_messages(_callback);
      });
    },

    /**
     * @name _deliver_incoming_messages:
     */
    _deliver_incoming_messages: function (_callback) {

      var self = this;
      var undelivered_messages = [];

      async.each(self._inbound_queue,

        function (_message, _next_fn) {
          self._notify_receive(_message, function (_error) {

            /* Error status:
                If there was an error in delivery, we hang on to
                the message and retry delivery at a later time. */

            if (_error) {
              undelivered_messages.push(_message);
            }

            /* All finished */
            _next_fn();
          });
        },
        function (_err) {

          /* Finish up:
              Replace the inbound queue with the messages we didn't
              get an okay on; the next queue run will retry them. */

          self._inbound_queue = undelivered_messages;
          return _callback.call(self);
        }
      );
    },

    /**
     * @name _notify_transmit:
     *   Invoke events appropriately for a successfully-transmitted
     *   message. The `_message` argument is the original pre-send
     *   message object (from the outbound queue); `_result` is the
     *   object that `gammu-json` yielded after the message was sent.
     */
    _notify_transmit: function (_message, _result, _callback) {

      console.log('transmit', _message, _result);
      return _callback();
    },

    /**
     * @name _notify_receive:
     *   Invoke events appropriately for a completely-received message.
     *   The `_message` argument is the received message object (from the
     *   inbound queue). If delivery fails in the event handler we're
     *   dispatching to, and that event handler's owner wants us to retry
     *   later (say, because that owner is out of storage space), then that
     *   handler *must* call `_callback` with a node-style error argument.
     *   Otherwise, the event handler should call `_callback` with a null or
     *   not-present first argument, and we'll consider the message to be
     *   delivered and no longer be our responsibility.
     */
    _notify_receive: function (_message, _callback) {

      console.log('receive', _message);
      return _callback();
    },

    /**
     * @name _reassemble_messages:
     */
    _reassemble_messages: function (_callback) {

      var self = this;

    },
    /**
     * @name initialize:
     */
    initialize: function (_options) {

      var self = this;

      var options = (_options || {});

      self._handlers = {};
      self._options = options;

      self._inbound_queue = [];
      self._outbound_queue = [];
      self._partial_messages = {};

      self._is_polling = false;
      self._is_processing = false;

      self._poll_interval = (
        _.isNumber(options.interval) ?
          (options.interval * 1000) : 10000 /* Seconds to milliseconds */
      );

      /* Caller-provided prefix:
          If provided, add $PREFIX/bin to the environment's $PATH. */

      if (self._options.prefix) {
        self._setenv('PATH', function (_value) {
          return (
            (_value || '') + ':' +
              path.resolve(self._options.prefix, 'bin')
          );
        });
      }

      return self;
    },

    /**
     * @name start:
     *   Start sending/receiving messages.
     */
    start: function () {

      this._start_polling();
    },

    /**
     * @name stop:
     *   Stop sending/receiving messages.
     */
    stop: function () {

      this._stop_polling();
    },

    /**
     * @name send:
     *   Send a message to one or more recipients.
     */
    send: function (_to, _message, _context, _transmit_callback) {
 
      /* Fix up arguments:
          This allows `_context` to be optionally omitted. */

      if (!_after_transmit_callback) {
        _transmit_callback = _context;
        _context = false;
      }

      /* Perform sanity checks:
          Arguments aren't vectorized; don't pass arrays in. */

      if (_transmit_callback && !_.isFunction(_transmit_callback)) {
        throw new Error('Callback, if provided, must be a function');
      }

      if (!_.isString(_to)) {
        throw new Error('Destination must be supplied as a string');
      }

      if (!_.isString(_message)) {
        throw new Error('Message text must be supplied as a string');
      }

      /* Push on to work queue:
          This queue is consumed by `_send_messages`. */

      this._outbound_queue.push({
        to: _to,
        message: _message,
        context: _context,
        callback: _transmit_callback
      });

      return this;
    },

    /**
     * @name on:
     *   Register an event-handling callback function. Valid events are
     *   `receive` (for being notified of single-part and fully-reassembled
     *   messages); `transmit` (for being notified of when a sent message
     *   has been successfully handed off to the telco for further
     *   transmission); `receive-part` (for being notified of the receipt of
     *   each individual part of a multipart/concatenated message); and
     *   `retrieve-part` (invoked during message reassembly if a
     *   previously-received message part is needed to aid the reassembly
     *   process).
     *
     *   To obtain full support for multipart message reassembly, you *must*
     *   handle both the `receive-part` and `retrieve-part` events.  The
     *   `receive-part` callback must write the message part to persistent
     *   storage before returning; the `retrieve-part` callback must fetch
     *   and return a previously-stored message part.
     */
    on: function (_event, _callback) {

      switch (_event) {
        case 'receive':
        case 'transmit':
        case 'receive-part':
        case 'retrieve-part':
          this._handlers[_event] = _callback;
          break;
        default:
          throw new Error('Invalid event specified');
          break;
      }

      return this;
    },

    /**
     * @name subprocess:
     *   Start a JSON-generating subprocess, wait for it to finish,
     *   and then return process's (parsed) output as an object.
     */
    _subprocess: function (_path, _argv, _options, _callback) {

      var json = '', errors = '';
      var subprocess = child.spawn(_path, _argv, { stdio: 'pipe' });
 
      /* Fix up arguments:
       *   This allows `_options` to be optionally omitted. */

      if (!_callback) {
        _callback = _options;
        _options = {};
      }

      subprocess.stdout.on('data', function (_buffer) {
        json += _buffer.toString();
      });

      subprocess.on('exit', function (_code, _signal) {

        var rv = false;

        if (_code != 0) {
          return _callback(
            new Error('Subprocess exited with non-zero status', _code)
          );
        }

        try {
          rv = JSON.parse(json);
        } catch (e) {
          return _callback(
            new Error('Subprocess produced invalid/incomplete JSON', e)
          );
        }

        return _callback(null, rv);
      });
      
      subprocess.stdin.end();
    }
};


/**
 * @name create:
 */
exports.create = function (/* ... */) {

  var klass = function (_arguments) {
    return this.initialize.apply(this, _arguments);
  };

  klass.prototype = _.extend({}, exports.prototype);
  return new klass(arguments);
};


/* Debug code */

var m = exports.create({
  prefix: '/srv/software/medic-core/v1.2.1/x86'
});

m.on('receive', function (_sms, _callback) {
});

m.on('transmit', function (_sms, _result, _callback) {
});

m.on('receive-part', function (_part, _callback) {
});

m.on('retrieve-part', function (_id, _callback) {
});

m.start();

