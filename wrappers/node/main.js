
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

      /* Check for termination */
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
     * @name _send_messages:
     */
    _send_messages: function (_callback) {

      var self = this;

      if (self._outbound_messages.length <= 0) {
        return _callback();
      }

      return _callback();
    },

    /**
     * @name _receive_messages:
     */
    _receive_messages: function (_callback) {

      var self = this;

      self._subprocess('gammu-json', [ 'retrieve' ], function (_err, _rv) {
        return _callback();
      });
    },

    /**
     * @name initialize:
     */
    initialize: function (_options) {

      var self = this;

      var options = (_options || {});

      self._handlers = {};
      self._options = options;
      self._outbound_messages = {};

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
     * @name send:
     *   Send a message to one or more recipients.
     */
    send: function (_to, _message, _context, _callback) {
 
      /* Fix up arguments:
       *   This allows `_context` to be optionally omitted. */

      if (!_callback) {
        _callback = _context;
        _context = false;
      }
    },

    /**
     * @name on:
     *   Register an event-handling callback function.
     */
    on: function (_event, _callback) {

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

m.on('receive', function () {
});

m.on('transmit', function () {
});

m.start();

