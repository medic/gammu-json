
var _ = require('underscore'),
    jsdump = require('jsDump'),
    moment = require('moment'),
    async = require('async');

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
     * @name initialize:
     */
    initialize: function (_options) {

      var self = this;

      self._options = (_options || {});

      /* Caller-provided prefix:
          If provided, add $PREFIX/bin to the environment's $PATH. */

      if (self._options.prefix) {
        setenv('PATH', function (_value) {
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

      var self = this;
    },

    /**
     * @name on:
     *   Register an event-handling callback function.
     */
    on: function (_event, _callback) {

      return this;
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

var m = exports.create();

m.on('receive', function () {
});

m.on('transmit', function () {
});

m.start();

