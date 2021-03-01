    // C function void glGetActiveAttrib ( GLuint program, GLuint index, GLsizei bufsize, GLsizei *length, GLint *size, GLenum *type, char *name )

    public static native void glGetActiveAttrib(
        int program,
        int index,
        int bufsize,
        int[] length,
        int lengthOffset,
        int[] size,
        int sizeOffset,
        int[] type,
        int typeOffset,
        byte[] name,
        int nameOffset
    );

    // C function void glGetActiveAttrib ( GLuint program, GLuint index, GLsizei bufsize, GLsizei *length, GLint *size, GLenum *type, char *name )

    /** @hide Method is broken, but used to be public (b/6006380) */
    public static native void glGetActiveAttrib(
        int program,
        int index,
        int bufsize,
        java.nio.IntBuffer length,
        java.nio.IntBuffer size,
        java.nio.IntBuffer type,
        byte name
    );

    // C function void glGetActiveAttrib ( GLuint program, GLuint index, GLsizei bufsize, GLsizei *length, GLint *size, GLenum *type, char *name )

    public static native String glGetActiveAttrib(
        int program,
        int index,
        int[] size,
        int sizeOffset,
        int[] type,
        int typeOffset
    );

    // C function void glGetActiveAttrib ( GLuint program, GLuint index, GLsizei bufsize, GLsizei *length, GLint *size, GLenum *type, char *name )

    public static native String glGetActiveAttrib(
        int program,
        int index,
        java.nio.IntBuffer size,
        java.nio.IntBuffer type
    );
