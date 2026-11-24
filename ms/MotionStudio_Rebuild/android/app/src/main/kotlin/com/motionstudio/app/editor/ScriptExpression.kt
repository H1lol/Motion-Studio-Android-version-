package com.motionstudio.app.editor

import kotlin.math.abs
import kotlin.math.ceil
import kotlin.math.floor
import kotlin.math.max
import kotlin.math.min
import kotlin.math.round
import kotlin.math.sin
import kotlin.math.cos
import kotlin.math.tan
import kotlin.math.sqrt

/**
 * The expression language behind the SCRIPT workspace.
 *
 * It is deliberately small and total: numbers, `t` (seconds inside the clip),
 * `fps`, `pi`, arithmetic with `+ - * / % ^`, parentheses, and a handful of
 * math functions. Every error is a [Error] with a readable message, so the pane
 * can tell the user what is wrong instead of failing silently.
 *
 * This is not a JavaScript interpreter and does not pretend to be one: an
 * expression is sampled once per frame and baked into keys, which the native
 * renderer already knows how to evaluate. Nothing here runs on the render path.
 */
object ScriptExpression {

    class Error(message: String) : Exception(message)

    fun evaluate(source: String, t: Double, fps: Double = 30.0): Double {
        val parser = Parser(tokenize(source), t, fps)
        val value = parser.expression()
        parser.expectEnd()
        return value
    }

    // ---- tokenizer ---------------------------------------------------------

    private sealed interface Token
    private data class Number(val value: Double) : Token
    private data class Name(val value: String) : Token
    private data class Op(val value: Char) : Token

    private fun tokenize(source: String): List<Token> {
        val tokens = ArrayList<Token>()
        var i = 0
        while (i < source.length) {
            val c = source[i]
            when {
                c.isWhitespace() -> i++
                c.isDigit() || (c == '.' && i + 1 < source.length && source[i + 1].isDigit()) -> {
                    val start = i
                    while (i < source.length && (source[i].isDigit() || source[i] == '.')) i++
                    val text = source.substring(start, i)
                    val value = text.toDoubleOrNull()
                        ?: throw Error("'$text' is not a number")
                    tokens += Number(value)
                }
                c.isLetter() || c == '_' -> {
                    val start = i
                    while (i < source.length && (source[i].isLetterOrDigit() || source[i] == '_')) i++
                    tokens += Name(source.substring(start, i))
                }
                c in "+-*/%^()," -> {
                    tokens += Op(c)
                    i++
                }
                else -> throw Error("unexpected character '$c'")
            }
        }
        if (tokens.isEmpty()) throw Error("the expression is empty")
        return tokens
    }

    // ---- recursive descent -------------------------------------------------

    private class Parser(
        private val tokens: List<Token>,
        private val t: Double,
        private val fps: Double,
    ) {
        private var index = 0

        fun expression(): Double {
            var value = term()
            while (true) {
                val op = peekOp()
                if (op == '+' || op == '-') {
                    index++
                    val rhs = term()
                    value = if (op == '+') value + rhs else value - rhs
                } else {
                    return value
                }
            }
        }

        private fun term(): Double {
            var value = power()
            while (true) {
                val op = peekOp()
                if (op == '*' || op == '/' || op == '%') {
                    index++
                    val rhs = power()
                    value = when (op) {
                        '*' -> value * rhs
                        '/' -> if (rhs == 0.0) Double.NaN else value / rhs
                        else -> if (rhs == 0.0) Double.NaN else value % rhs
                    }
                } else {
                    return value
                }
            }
        }

        private fun power(): Double {
            val base = unary()
            if (peekOp() == '^') {
                index++
                return Math.pow(base, power())
            }
            return base
        }

        private fun unary(): Double {
            val op = peekOp()
            if (op == '-') {
                index++
                return -unary()
            }
            if (op == '+') {
                index++
                return unary()
            }
            return primary()
        }

        private fun primary(): Double {
            val token = tokens.getOrNull(index) ?: throw Error("the expression ends too early")
            index++
            return when (token) {
                is Number -> token.value
                is Op -> {
                    if (token.value != '(') throw Error("expected a value, found '${token.value}'")
                    val value = expression()
                    if (peekOp() != ')') throw Error("missing a closing ')'")
                    index++
                    value
                }
                is Name -> {
                    val name = token.value.lowercase()
                    if (peekOp() == '(') {
                        index++
                        callFunction(name)
                    } else {
                        constant(name)
                    }
                }
            }
        }

        private fun callFunction(name: String): Double {
            val args = ArrayList<Double>()
            if (peekOp() != ')') {
                while (true) {
                    args += expression()
                    if (peekOp() == ',') {
                        index++
                        continue
                    }
                    break
                }
            }
            if (peekOp() != ')') throw Error("missing a closing ')' after $name(")
            index++
            fun need(count: Int) {
                if (args.size != count) {
                    throw Error("$name() takes $count argument" + if (count == 1) "" else "s")
                }
            }
            return when (name) {
                "sin" -> { need(1); sin(args[0]) }
                "cos" -> { need(1); cos(args[0]) }
                "tan" -> { need(1); tan(args[0]) }
                "abs" -> { need(1); abs(args[0]) }
                "sqrt" -> { need(1); if (args[0] < 0) Double.NaN else sqrt(args[0]) }
                "floor" -> { need(1); floor(args[0]) }
                "ceil" -> { need(1); ceil(args[0]) }
                "round" -> { need(1); round(args[0]) }
                "min" -> { need(2); min(args[0], args[1]) }
                "max" -> { need(2); max(args[0], args[1]) }
                "pow" -> { need(2); Math.pow(args[0], args[1]) }
                "clamp" -> { need(3); args[0].coerceIn(min(args[1], args[2]), max(args[1], args[2])) }
                else -> throw Error("unknown function '$name'")
            }
        }

        private fun constant(name: String): Double = when (name) {
            "t", "time", "s" -> t
            "fps" -> fps
            "pi" -> Math.PI
            "e" -> Math.E
            else -> throw Error("unknown name '$name' (use t, fps, pi or a function)")
        }

        private fun peekOp(): Char? = (tokens.getOrNull(index) as? Op)?.value

        fun expectEnd() {
            if (index != tokens.size) {
                val left = tokens.getOrNull(index)
                val what = when (left) {
                    is Op -> "'${left.value}'"
                    is Name -> "'${left.value}'"
                    is Number -> "a number"
                    null -> "nothing"
                }
                throw Error("unexpected $what at the end")
            }
        }
    }
}
