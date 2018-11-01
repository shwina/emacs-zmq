#include "emacs-zmq.h"

int plugin_is_GPL_compatible;

typedef struct {
    char *name;
    void *fun;
    char const * doc;
    ptrdiff_t minarity;
    ptrdiff_t maxarity;
} ezmq_fun_t;

emacs_env *env = NULL;
emacs_value Qzmq_error, Qt, Qnil, Qnth, Qlist,
    Qwrong_type_argument, Qargs_out_of_range,
    Qcons, Qstring, Qvector, Qcar, Qcdr, Qlength, Qinteger, Qequal,
    Qzmq_POLLIN, Qzmq_POLLERR, Qzmq_POLLOUT,
    Izmq_POLLIN, Izmq_POLLERR, Izmq_POLLOUT;

#define EZMQ_MAXARGS 5

static emacs_value _fargs[EZMQ_MAXARGS];

/**
   Dispatch to the intended C function from a call to a ZMQ function in Emacs.

   CURRENT_ENV is the current Emacs environment for the dispatch call. NARGS is
   the number of arguments for this call whose values are contained in ARGS.
   INFO is the ezmq_fun_t* which holds the function to dispatch to.
*/
static emacs_value
ezmq_dispatch(emacs_env *current_env, ptrdiff_t nargs, emacs_value args[], void *info)
{
    // Set the global environment for this dispatch call.
    env = current_env;

    // Ensure the number of arguments are within range
    emacs_value ret = Qnil;
    if(nargs > EZMQ_MAXARGS) {
        // Better error
        ezmq_signal(Qargs_out_of_range, 2, INT(nargs), INT(EZMQ_MAXARGS));
        return ret;
    }

    // Extract the function information
    void *fun = ((ezmq_fun_t *)info)->fun;
    ptrdiff_t maxargs = ((ezmq_fun_t *)info)->maxarity;

    if(nargs < maxargs) {
        // Fill in nil values for optional arguments
        int i;
        for(i = (maxargs - 1); i >= nargs; i--) {
            _fargs[i] = Qnil;
        }
        for(i = 0; i < nargs; i++) {
            _fargs[i] = args[i];
        }
        args = _fargs;
        nargs = maxargs;
    }

    // Dispatch based on number of arguments
    switch(nargs) {
    case 0:
        ret = ((emacs_value(*)(void))fun)();
        break;
    case 1:
        ret = ((emacs_value(*)(emacs_value))fun)(args[0]);
        break;
    case 2:
        ret = ((emacs_value(*)(emacs_value, emacs_value))fun)(args[0], args[1]);
        break;
    case 3:
        ret = ((emacs_value(*)(emacs_value, emacs_value,
                               emacs_value))fun)(args[0], args[1],
                                                 args[2]);
        break;
        // Currently unused
    case 4:
        ret = ((emacs_value(*)(emacs_value, emacs_value,
                               emacs_value, emacs_value))fun)(args[0], args[1],
                                                              args[2], args[3]);
        break;
    case EZMQ_MAXARGS:
        ret = ((emacs_value(*)(emacs_value, emacs_value,
                               emacs_value, emacs_value,
                               emacs_value))fun)(args[0], args[1],
                                                 args[2], args[3],
                                                 args[4]);
        break;
    }
    return ret;
}

static void
ezmq_bind_function(ezmq_fun_t *fun)
{
    emacs_value Sfun = env->make_function(env,
                                          fun->minarity, fun->maxarity,
                                          &ezmq_dispatch,
                                          fun->doc,
                                          fun);
    // Use defalias here so that at least we get a link to zmq.el when examining
    // the documentation for a zmq function.
    FUNCALL(INTERN("defalias"), 2, ((emacs_value []){INTERN(fun->name), Sfun}));
}


static void
ezmq_provide(const char *feature)
{
    FUNCALL(INTERN("provide"), 1, ((emacs_value []){ INTERN(feature) }));
}

#define EZMQ_DEFERR(err)                        \
    args[0] = INTERN("zmq-"#err);               \
    args[1] = STRING(#err, sizeof(#err) - 1);   \
    FUNCALL(Qdefine_error, 3, args)             \

/**
   Make error symbols for common C errors used by ZMQ. Each error symbol is the
   C error with a zmq- prefix. So EAGAIN becomes zmq-EAGAIN in Emacs.
*/
static void
ezmq_make_error_symbols()
{
    emacs_value Qdefine_error = INTERN("define-error");
    const char *msg =  "An error occured in ZMQ";
    emacs_value args[3];

    // Define the root error symbol for ZMQ errors
    args[0] = Qzmq_error;
    args[1] = STRING(msg, strlen(msg));
    args[2] = INTERN("error");
    FUNCALL(Qdefine_error, 3, args);

    // Set zmq-ERROR as the root error for all errors defined below
    args[2] = Qzmq_error;
    // Define common errors as symbols
    // Also see zmq_signal_error
    EZMQ_DEFERR(EINVAL);
    EZMQ_DEFERR(EPROTONOSUPPORT);
    EZMQ_DEFERR(ENOCOMPATPROTO);
    EZMQ_DEFERR(EADDRINUSE);
    EZMQ_DEFERR(EADDRNOTAVAIL);
    EZMQ_DEFERR(ENODEV);
    EZMQ_DEFERR(ETERM);
    EZMQ_DEFERR(ENOTSOCK);
    EZMQ_DEFERR(EMTHREAD);
    EZMQ_DEFERR(EFAULT);
    EZMQ_DEFERR(EINTR);
    EZMQ_DEFERR(ENOTSUP);
    EZMQ_DEFERR(ENOENT);
    EZMQ_DEFERR(ENOMEM);
    EZMQ_DEFERR(EAGAIN);
    EZMQ_DEFERR(EFSM);
    EZMQ_DEFERR(EHOSTUNREACH);
    EZMQ_DEFERR(EMFILE);
}

#define EZMQ_DEFFUN(ename, cfun, argmin, argmax)  \
    do {                                          \
        _info.name = ename;                       \
        _info.fun = cfun;                         \
        _info.minarity = argmin;                  \
        _info.maxarity = argmax;                  \
        _info.doc = __zmq_doc_##cfun;             \
        info = malloc(sizeof(ezmq_fun_t));        \
        memcpy(info, &_info, sizeof(ezmq_fun_t)); \
        ezmq_bind_function(info);                 \
    } while (0)

static void
ezmq_define_functions()
{
    ezmq_fun_t *info = malloc(sizeof(ezmq_fun_t));
    ezmq_fun_t _info;
    EZMQ_DEFFUN("zmq-socket", ezmq_socket, 2, 2);
    EZMQ_DEFFUN("zmq-send", ezmq_send, 2, 3);
    EZMQ_DEFFUN("zmq-recv", ezmq_recv, 1, 3);
    EZMQ_DEFFUN("zmq-bind", ezmq_bind, 2, 2);
    EZMQ_DEFFUN("zmq-connect", ezmq_connect, 2, 2);
    EZMQ_DEFFUN("zmq-unbind", ezmq_unbind, 2, 2);
    EZMQ_DEFFUN("zmq-disconnect", ezmq_disconnect, 2, 2);
    EZMQ_DEFFUN("zmq-close", ezmq_close, 1, 1);
    EZMQ_DEFFUN("zmq-proxy", ezmq_proxy, 2, 3);
    EZMQ_DEFFUN("zmq-proxy-steerable", ezmq_proxy_steerable, 2, 4);
    EZMQ_DEFFUN("zmq-socket-monitor", ezmq_socket_monitor, 3, 3);
    EZMQ_DEFFUN("zmq-socket-set", ezmq_setsockopt, 3, 3);
    EZMQ_DEFFUN("zmq-socket-get", ezmq_getsockopt, 2, 2);
    EZMQ_DEFFUN("zmq-context", ezmq_context, 0, 0);
    EZMQ_DEFFUN("zmq-context-terminate", ezmq_ctx_term, 1, 1);
    EZMQ_DEFFUN("zmq-context-shutdown", ezmq_ctx_shutdown, 1, 1);
    EZMQ_DEFFUN("zmq-context-get", ezmq_ctx_get, 2, 2);
    EZMQ_DEFFUN("zmq-context-set", ezmq_ctx_set, 3, 3);
    EZMQ_DEFFUN("zmq-message", ezmq_message, 0, 1);
    EZMQ_DEFFUN("zmq-message-size", ezmq_message_size, 1, 1);
    EZMQ_DEFFUN("zmq-message-data", ezmq_message_data, 1, 1);
    EZMQ_DEFFUN("zmq-message-more-p", ezmq_message_more, 1, 1);
    EZMQ_DEFFUN("zmq-message-copy", ezmq_message_copy, 1, 1);
    EZMQ_DEFFUN("zmq-message-move", ezmq_message_move, 2, 2);
    EZMQ_DEFFUN("zmq-message-close", ezmq_message_close, 1, 1);
    EZMQ_DEFFUN("zmq-message-set", ezmq_message_set, 3, 3);
    EZMQ_DEFFUN("zmq-message-get", ezmq_message_get, 2, 2);
    EZMQ_DEFFUN("zmq-message-recv", ezmq_message_recv, 2, 3);
    EZMQ_DEFFUN("zmq-message-send", ezmq_message_send, 2, 3);
    EZMQ_DEFFUN("zmq-message-gets", ezmq_message_gets, 2, 2);
    EZMQ_DEFFUN("zmq-message-routing-id", ezmq_message_routing_id, 1, 1);
    EZMQ_DEFFUN("zmq-message-set-routing-id", ezmq_message_set_routing_id, 2, 2);
    EZMQ_DEFFUN("zmq-poll", ezmq_poll, 2, 2);
    EZMQ_DEFFUN("zmq-poller", ezmq_poller_new, 0, 0);
    EZMQ_DEFFUN("zmq-poller-add", ezmq_poller_add, 3, 3);
    EZMQ_DEFFUN("zmq-poller-modify", ezmq_poller_modify, 3, 3);
    EZMQ_DEFFUN("zmq-poller-remove", ezmq_poller_remove, 2, 2);
    EZMQ_DEFFUN("zmq-poller-destroy", ezmq_poller_destroy, 1, 1);
    EZMQ_DEFFUN("zmq-poller-wait", ezmq_poller_wait, 2, 2);
    EZMQ_DEFFUN("zmq-poller-wait-all", ezmq_poller_wait_all, 3, 3);
    EZMQ_DEFFUN("zmq-version", ezmq_version, 0, 0);
    EZMQ_DEFFUN("zmq-has", ezmq_has, 1, 1);
    EZMQ_DEFFUN("zmq-z85-decode", ezmq_z85_decode, 1, 1);
    EZMQ_DEFFUN("zmq-z85-encode", ezmq_z85_encode, 1, 1);
    EZMQ_DEFFUN("zmq-curve-keypair", ezmq_curve_keypair, 0, 0);
    EZMQ_DEFFUN("zmq-curve-public", ezmq_curve_public, 1, 1);
    EZMQ_DEFFUN("zmq-equal", ezmq_equal, 2, 2);
    EZMQ_DEFFUN("zmq-message-p", ezmq_message_p, 1, 1);
    EZMQ_DEFFUN("zmq-socket-p", ezmq_socket_p, 1, 1);
    EZMQ_DEFFUN("zmq-context-p", ezmq_context_p, 1, 1);
    EZMQ_DEFFUN("zmq-poller-p", ezmq_poller_p, 1, 1);
}

static bool initialized = false;

int
emacs_module_init(struct emacs_runtime *ert)
{
    if(initialized)
        return 0;

    // Retrieve the current emacs environment
    env = ert->get_environment(ert);

    Qt = INTERN("t");
    Qnil = INTERN("nil");
    Qnth = INTERN("nth");
    Qwrong_type_argument = INTERN("wrong-type-argument");
    Qargs_out_of_range = INTERN("args-out-of-range");
    Qlist = INTERN("list");
    Qstring = INTERN("string");
    Qvector = INTERN("vector");
    Qcons = INTERN("cons");
    Qcar = INTERN("car");
    Qcdr = INTERN("cdr");
    Qequal = INTERN("equal");
    Qinteger = INTERN("integer");
    Qlength = INTERN("length");
    Qzmq_error = GLOBREF(INTERN("zmq-ERROR"));

    ezmq_make_error_symbols();

    ezmq_expose_constants();
    Qzmq_POLLIN = GLOBREF(INTERN("zmq-POLLIN"));
    Qzmq_POLLOUT = GLOBREF(INTERN("zmq-POLLOUT"));
    Qzmq_POLLERR = GLOBREF(INTERN("zmq-POLLERR"));

    emacs_value Qsval = INTERN("symbol-value");
    Izmq_POLLIN = FUNCALL(Qsval, 1, &Qzmq_POLLIN);
    Izmq_POLLOUT = FUNCALL(Qsval, 1, &Qzmq_POLLOUT);
    Izmq_POLLERR = FUNCALL(Qsval, 1, &Qzmq_POLLERR);

    ezmq_define_functions();

    ezmq_provide("zmq-core");

    initialized = true;
    return 0;
}
