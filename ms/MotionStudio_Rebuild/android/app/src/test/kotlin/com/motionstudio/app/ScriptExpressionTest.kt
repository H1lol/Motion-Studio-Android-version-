package com.motionstudio.app

import com.motionstudio.app.editor.ScriptExpression
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.PI

class ScriptExpressionTest {

    private fun eval(source: String, t: Double = 0.0): Double =
        ScriptExpression.evaluate(source, t, 30.0)

    @Test
    fun arithmeticRespectsPrecedence() {
        assertEquals(7.0, eval("1 + 2 * 3"), 1e-9)
        assertEquals(9.0, eval("(1 + 2) * 3"), 1e-9)
        assertEquals(-5.0, eval("-5"), 1e-9)
        assertEquals(8.0, eval("2 ^ 3"), 1e-9)
    }

    @Test
    fun timeVariableFeedsTheExpression() {
        assertEquals(3.0, eval("t * 2", t = 1.5), 1e-9)
        assertEquals(2.0 * PI, eval("2*pi"), 1e-9)
        assertEquals(30.0, eval("fps"), 1e-9)
    }

    @Test
    fun fadeShapeIsOneAtTheEnd() {
        assertEquals(1.0, eval("clamp(t / 1.0, 0, 1)", t = 2.0), 1e-9)
        assertEquals(0.25, eval("clamp(t / 1.0, 0, 1)", t = 0.25), 1e-9)
    }

    @Test
    fun functionsAndConstantsResolve() {
        assertEquals(1.0, eval("sin(pi/2)"), 1e-9)
        assertEquals(3.0, eval("max(1, 3)"), 1e-9)
        assertEquals(2.0, eval("sqrt(4)"), 1e-9)
        assertEquals(1.0, eval("round(0.6)"), 1e-9)
    }

    @Test
    fun divisionByZeroIsNotANumber() {
        assertTrue(eval("1 / 0").isNaN())
        assertTrue(eval("1 % 0").isNaN())
    }

    @Test
    fun unknownNamesAreReported() {
        val error = runCatching { eval("velocity * 2") }.exceptionOrNull()
        assertTrue(error is ScriptExpression.Error)
        assertTrue(error!!.message!!.contains("velocity"))
    }

    @Test
    fun missingParenthesisIsReported() {
        val error = runCatching { eval("sin(t") }.exceptionOrNull()
        assertTrue(error is ScriptExpression.Error)
    }
}
