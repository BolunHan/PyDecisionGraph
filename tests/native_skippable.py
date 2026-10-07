import dis
import struct
import array
import types
import functools
from typing import Callable, Any, Iterator, Tuple


class SkippableCtx:
    def __init__(self, skip: bool = False):
        self.skip = skip
        self.exc = type(f"{self.__class__.__name__}SkipException", (Exception,), {"owner": self})
        print(f"[DEBUG] SkippableCtx.__init__(skip={skip})")

    def __enter__(self):
        print(f"[DEBUG] SkippableCtx.__enter__() called, skip={self.skip} — NOT raising (expect injected check)")
        return self

    def __exit__(self, exc_type, exc_value, exc_traceback):
        if exc_type is None:
            return
        elif issubclass(exc_type, self.exc):
            return True
        return False

    def _skippable_check(self):
        if self.skip:
            raise self.exc()

    @staticmethod
    def eval(func: Callable) -> Callable:
        print(f"[DEBUG] Decorating {func.__name__} with SkippableCtx.eval")
        code = func.__code__

        # Step 1: inject _skippable_check into co_consts if not already present
        consts = code.co_consts
        if _skippable_check not in consts:
            new_consts = consts + (_skippable_check,)
            check_index = len(consts)
        else:
            check_index = consts.index(_skippable_check)
            new_consts = consts

        # Step 2: patch code using this check_index as LOAD_CONST target
        new_code = SkippableCtx._patch_function_code_312(code, check_index, new_consts)
        if new_code is code:
            return func

        # Step 3: build new function
        return functools.update_wrapper(
            types.FunctionType(
                new_code,
                func.__globals__,
                func.__name__,
                func.__defaults__,
                func.__closure__,
            ),
            func
        )

    @staticmethod
    def _patch_function_code_312(code: types.CodeType, check_index: int, new_consts: tuple) -> types.CodeType:
        co_code = code.co_code
        co_names = code.co_names

        if 'SkippableCtx' not in co_names:
            print("[DEBUG] SkippableCtx not used in function")
            return code

        skippable_idx = co_names.index('SkippableCtx')
        instructions = list(_parse_instructions(co_code))
        buf = array.array('B', co_code)
        patched = False

        i = 0
        while i < len(instructions):
            opname, _, offset = instructions[i]
            if opname == 'BEFORE_WITH':
                # Look back for LOAD_GLOBAL SkippableCtx before the CALL that created it
                j = i - 1
                found = False
                while j >= 0:
                    back_op, back_arg, _ = instructions[j]
                    if back_op == 'LOAD_GLOBAL' and back_arg == skippable_idx:
                        found = True
                        break
                    if back_op in ('RESUME', 'RETURN_CONST'):
                        break
                    j -= 1
                if found and i + 1 < len(instructions):
                    next_op, _, pop_offset = instructions[i + 1]
                    if next_op == 'POP_TOP' or next_op == 'STORE_FAST':
                        print(f"[DEBUG] Patching after BEFORE_WITH at offset {offset}")
                        # Replace POP_TOP (2 bytes) with:
                        # DUP_TOP; LOAD_CONST check; ROT_TWO; CALL 1; POP_TOP
                        new_ops = [
                            'DUP_TOP',
                            ('LOAD_CONST', check_index),
                            'ROT_TWO',
                            ('CALL', 1),
                            'POP_TOP'
                        ]
                        old_size = _get_instruction_size('POP_TOP')
                        new_size = sum(_get_instruction_size(op if isinstance(op, str) else op[0],
                                                             op[1] if isinstance(op, tuple) else 0)
                                       for op in new_ops)
                        if new_size <= old_size:
                            pos = pop_offset
                            for op in new_ops:
                                if isinstance(op, str):
                                    pos = _write_instruction(buf, pos, op)
                                else:
                                    pos = _write_instruction(buf, pos, op[0], op[1])
                            while pos < pop_offset + old_size:
                                pos = _write_instruction(buf, pos, 'NOP')
                            patched = True
                            print("[DEBUG] Patch applied!")
                        else:
                            print(f"[ERROR] Not enough space: need {new_size}, have {old_size}")
            i += 1

        if not patched:
            return code

        # Rebuild code with new consts and increased stacksize
        return types.CodeType(
            code.co_argcount,
            getattr(code, 'co_kwonlyargcount', 0),
            code.co_nlocals,
            max(code.co_stacksize + 3, 10),
            code.co_flags,
            _array_to_bytes(buf),
            new_consts,
            code.co_names,
            code.co_varnames,
            code.co_filename,
            code.co_name,
            code.co_firstlineno,
            code.co_lnotab,
            code.co_freevars,
            code.co_cellvars,
        )


class _Bytecode:
    def __init__(self):
        code = (lambda: 1 if 0 else 2).__code__.co_code
        opcode = struct.unpack_from('B', code, 2)[0]

        if dis.opname[opcode] == 'LOAD_CONST':
            print("[DEBUG] Detected Python 3.6+ wordcode format")
            self.argument = struct.Struct('B')
            self.have_argument = 90  # legacy value used for comparison
            self.opcode_size = 1
            self.arg_size = 1
            self.instruction_size = 2
            self.jump_unit = 1
        else:
            raise RuntimeError("[ERROR] Pre-3.6 bytecode not supported")

    @property
    def argument_bits(self):
        return self.arg_size * 8


_BYTECODE = _Bytecode()


def _skippable_check(ctx):
    print(f"[DEBUG] _skippable_check called, ctx.skip = {ctx.skip}")
    if ctx.skip:
        print("[DEBUG] Raising SkippedBlock!")
        raise SkippedBlock()


def _array_to_bytes(arr: array.array) -> bytes:
    return arr.tobytes() if hasattr(arr, 'tobytes') else arr.tostring()


def _parse_instructions(code_bytes: bytes) -> Iterator[Tuple[str, int, int]]:
    pos = 0
    extended_arg = 0
    while pos < len(code_bytes):
        offset = pos
        opcode = struct.unpack_from('B', code_bytes, pos)[0]
        pos += 1
        oparg = None

        if opcode >= _BYTECODE.have_argument:
            raw_arg = _BYTECODE.argument.unpack_from(code_bytes, pos)[0]
            pos += _BYTECODE.arg_size
            oparg = extended_arg | raw_arg

            if opcode == dis.opmap.get('EXTENDED_ARG', -1):
                extended_arg = oparg << _BYTECODE.argument_bits
                continue

        extended_arg = 0
        yield (dis.opname[opcode], oparg, offset)


def _get_instruction_size(opname: str, oparg: int = 0) -> int:
    size = _BYTECODE.opcode_size
    if dis.opmap[opname] >= _BYTECODE.have_argument:
        size += _BYTECODE.arg_size
        if oparg >= (1 << _BYTECODE.argument_bits):
            return _get_instruction_size('EXTENDED_ARG', oparg >> _BYTECODE.argument_bits) + size
    return size


def _write_instruction(buf: array.array, pos: int, opname: str, oparg: int = 0) -> int:
    opcode = dis.opmap[opname]
    if opcode >= _BYTECODE.have_argument:
        if oparg >= (1 << _BYTECODE.argument_bits):
            pos = _write_instruction(buf, pos, 'EXTENDED_ARG', oparg >> _BYTECODE.argument_bits)
            oparg &= (1 << _BYTECODE.argument_bits) - 1
    buf[pos] = opcode
    pos += 1
    if opcode >= _BYTECODE.have_argument:
        _BYTECODE.argument.pack_into(buf, pos, oparg)
        pos += _BYTECODE.arg_size
    return pos


@SkippableCtx.eval
def run_skippable_ctx_example():
    print('testing SkippableCtx with skip=True...')

    with SkippableCtx(skip=True) as ctx:
        print('never entered')  # should be skipped

    print('testing SkippableCtx with skip=False...')
    with SkippableCtx(skip=False):
        print('entered')  # should print

    print('All test complete!')


if __name__ == '__main__':
    run_skippable_ctx_example()
