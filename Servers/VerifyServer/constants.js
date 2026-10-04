let code_prefix = "code_";

const Errors = {
    SUCCESS: 0,
    REDIS_ERROR: 1,
    EXCEPTION: 2,
    VERIFICATION_CODE_EXISTS: 3,
}

module.exports = { code_prefix, Errors }