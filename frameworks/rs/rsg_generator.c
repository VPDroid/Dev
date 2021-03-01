
#include "spec.h"
#include <stdio.h>
#include <string.h>

void printFileHeader(FILE *f) {
    fprintf(f, "/*\n");
    fprintf(f, " * Copyright (C) 2011 The Android Open Source Project\n");
    fprintf(f, " *\n");
    fprintf(f, " * Licensed under the Apache License, Version 2.0 (the \"License\");\n");
    fprintf(f, " * you may not use this file except in compliance with the License.\n");
    fprintf(f, " * You may obtain a copy of the License at\n");
    fprintf(f, " *\n");
    fprintf(f, " *      http://www.apache.org/licenses/LICENSE-2.0\n");
    fprintf(f, " *\n");
    fprintf(f, " * Unless required by applicable law or agreed to in writing, software\n");
    fprintf(f, " * distributed under the License is distributed on an \"AS IS\" BASIS,\n");
    fprintf(f, " * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.\n");
    fprintf(f, " * See the License for the specific language governing permissions and\n");
    fprintf(f, " * limitations under the License.\n");
    fprintf(f, " */\n\n");
}

void printVarType(FILE *f, const VarType *vt) {
    int ct;
    if (vt->isConst) {
        fprintf(f, "const ");
    }

    switch (vt->type) {
    case 0:
        fprintf(f, "void");
        break;
    case 1:
        fprintf(f, "int%i_t", vt->bits);
        break;
    case 2:
        fprintf(f, "uint%i_t", vt->bits);
        break;
    case 3:
        if (vt->bits == 32)
            fprintf(f, "float");
        else
            fprintf(f, "double");
        break;
    case 4:
        fprintf(f, "%s", vt->typeName);
        break;
    }

    if (vt->ptrLevel) {
        fprintf(f, " ");
        for (ct=0; ct < vt->ptrLevel; ct++) {
            fprintf(f, "*");
        }
    }
}

void printVarTypeAndName(FILE *f, const VarType *vt) {
    printVarType(f, vt);

    if (vt->name[0]) {
        fprintf(f, " %s", vt->name);
    }
}

void printArgList(FILE *f, const ApiEntry * api, int assumePrevious) {
    int ct;
    for (ct=0; ct < api->paramCount; ct++) {
        if (ct || assumePrevious) {
            fprintf(f, ", ");
        }
        printVarTypeAndName(f, &api->params[ct]);
    }
}

void printStructures(FILE *f) {
    int ct;
    int ct2;

    for (ct=0; ct < apiCount; ct++) {
        fprintf(f, "typedef struct RS_CMD_%s_rec RS_CMD_%s;\n", apis[ct].name, apis[ct].name);
    }
    fprintf(f, "\n");

    for (ct=0; ct < apiCount; ct++) {
        const ApiEntry * api = &apis[ct];
        fprintf(f, "#define RS_CMD_ID_%s %i\n", api->name, ct+1);
        fprintf(f, "struct __attribute__((packed)) RS_CMD_%s_rec {\n", api->name);
        //fprintf(f, "    RsCommandHeader _hdr;\n");

        for (ct2=0; ct2 < api->paramCount; ct2++) {
            fprintf(f, "    ");
            printVarTypeAndName(f, &api->params[ct2]);
            fprintf(f, ";\n");
        }
        fprintf(f, "};\n\n");
    }
}

void printFuncDecl(FILE *f, const ApiEntry *api, const char *prefix, int addContext, int isFnPtr) {
    printVarTypeAndName(f, &api->ret);
    if (isFnPtr) {
        char t[1024];
        strcpy(t, api->name);
        if (strlen(prefix) == 0) {
            if (t[0] > 'A' && t[0] < 'Z') {
                t[0] -= 'A' - 'a';
            }
        }
        fprintf(f, " (* %s%s) (", prefix, api->name);
    } else {
        fprintf(f, " %s%s (", prefix, api->name);
    }
    if (!api->nocontext) {
        if (addContext) {
            fprintf(f, "Context *");
        } else {
            fprintf(f, "RsContext rsc");
        }
    }
    printArgList(f, api, !api->nocontext);
    fprintf(f, ")");
}

void printFuncDecls(FILE *f, const char *prefix, int addContext, int externC) {
    int ct;
    for (ct=0; ct < apiCount; ct++) {
        if (externC) {
            fprintf(f, "extern \"C\" ");
        }
        printFuncDecl(f, &apis[ct], prefix, addContext, 0);
        fprintf(f, ";\n");
    }
    fprintf(f, "\n\n");
}

void printFuncPointers(FILE *f, int addContext) {
    fprintf(f, "\n");
    fprintf(f, "typedef struct RsApiEntrypoints {\n");
    int ct;
    for (ct=0; ct < apiCount; ct++) {
        fprintf(f, "    ");
        printFuncDecl(f, &apis[ct], "", addContext, 1);
        fprintf(f, ";\n");
    }
    fprintf(f, "} RsApiEntrypoints_t;\n\n");
}

void printPlaybackFuncs(FILE *f, const char *prefix) {
    int ct;
    for (ct=0; ct < apiCount; ct++) {
        if (apis[ct].direct) {
            continue;
        }

        fprintf(f, "void %s%s (Context *, const void *);\n", prefix, apis[ct].name);
    }
}

static int hasInlineDataPointers(const ApiEntry * api) {
    int ret = 0;
    int ct;
    if (api->sync || api->ret.typeName[0]) {
        return 0;
    }
    for (ct=0; ct < api->paramCount; ct++) {
        const VarType *vt = &api->params[ct];

        if (!vt->isConst && vt->ptrLevel) {
            // Non-const pointers cannot be inlined.
            return 0;
        }
        if (vt->ptrLevel > 1) {
            // not handled yet.
            return 0;
        }

        if (vt->isConst && vt->ptrLevel) {
            // Non-const pointers cannot be inlined.
            ret = 1;
        }
    }
    return ret;
}

void printApiCpp(FILE *f) {
    int ct;
    int ct2;

    fprintf(f, "#include \"rsDevice.h\"\n");
    fprintf(f, "#include \"rsContext.h\"\n");
    fprintf(f, "#include \"rsThreadIO.h\"\n");
    fprintf(f, "#include \"rsgApiStructs.h\"\n");
    fprintf(f, "#include \"rsgApiFuncDecl.h\"\n");
    fprintf(f, "#include \"rsFifo.h\"\n");
    fprintf(f, "\n");
    fprintf(f, "using namespace android;\n");
    fprintf(f, "using namespace android::renderscript;\n");
    fprintf(f, "\n");

    printFuncPointers(f, 0);

    // Generate RS funcs for local fifo
    for (ct=0; ct < apiCount; ct++) {
        int needFlush = 0;
        const ApiEntry * api = &apis[ct];

        fprintf(f, "static ");
        printFuncDecl(f, api, "LF_", 0, 0);
        fprintf(f, "\n{\n");
        if (api->direct) {
            fprintf(f, "    ");
            if (api->ret.typeName[0]) {
                fprintf(f, "return ");
            }
            fprintf(f, "rsi_%s(", api->name);
            if (!api->nocontext) {
                fprintf(f, "(Context *)rsc");
            }
            for (ct2=0; ct2 < api->paramCount; ct2++) {
                const VarType *vt = &api->params[ct2];
                if (ct2 > 0 || !api->nocontext) {
                    fprintf(f, ", ");
                }
                fprintf(f, "%s", vt->name);
            }
            fprintf(f, ");\n");
        } else if (api->handcodeApi) {
            // handle handcode path
            fprintf(f, "    LF_%s_handcode(", api->name);
            if (!api->nocontext) {
                fprintf(f, "(Context *)rsc");
            }
            for (ct2=0; ct2 < api->paramCount; ct2++) {
                const VarType *vt = &api->params[ct2];
                if (ct2 > 0 || !api->nocontext) {
                    fprintf(f, ", ");
                }
                fprintf(f, "%s", vt->name);
            }
            fprintf(f, ");\n");

        } else {
            // handle synchronous path
            fprintf(f, "    if (((Context *)rsc)->isSynchronous()) {\n");
            fprintf(f, "        ");
            if (api->ret.typeName[0]) {
                fprintf(f, "return ");
            }
            fprintf(f, "rsi_%s(", api->name);
            if (!api->nocontext) {
                fprintf(f, "(Context *)rsc");
            }
            for (ct2=0; ct2 < api->paramCount; ct2++) {
                const VarType *vt = &api->params[ct2];
                if (ct2 > 0 || !api->nocontext) {
                    fprintf(f, ", ");
                }
                fprintf(f, "%s", vt->name);
            }
            fprintf(f, ");\n");
            if (!api->ret.typeName[0]) {
                fprintf(f, "    return;");
            }
            fprintf(f, "    }\n\n");

            fprintf(f, "    ThreadIO *io = &((Context *)rsc)->mIO;\n");
            fprintf(f, "    const size_t size = sizeof(RS_CMD_%s);\n", api->name);
            if (hasInlineDataPointers(api)) {
                fprintf(f, "    size_t dataSize = 0;\n");
                for (ct2=0; ct2 < api->paramCount; ct2++) {
                    const VarType *vt = &api->params[ct2];
                    if (vt->isConst && vt->ptrLevel) {
                        fprintf(f, "    dataSize += %s_length;\n", vt->name);
                    }
                }
            }

            //fprintf(f, "    ALOGE(\"add command %s\\n\");\n", api->name);
            if (hasInlineDataPointers(api)) {
                fprintf(f, "    RS_CMD_%s *cmd = NULL;\n", api->name);
                fprintf(f, "    if (dataSize < io->getMaxInlineSize()) {;\n");
                fprintf(f, "        cmd = static_cast<RS_CMD_%s *>(io->coreHeader(RS_CMD_ID_%s, dataSize + size));\n", api->name, api->name);
                fprintf(f, "    } else {\n");
                fprintf(f, "        cmd = static_cast<RS_CMD_%s *>(io->coreHeader(RS_CMD_ID_%s, size));\n", api->name, api->name);
                fprintf(f, "    }\n");
                fprintf(f, "    uint8_t *payload = (uint8_t *)&cmd[1];\n");
            } else {
                fprintf(f, "    RS_CMD_%s *cmd = static_cast<RS_CMD_%s *>(io->coreHeader(RS_CMD_ID_%s, size));\n", api->name, api->name, api->name);
            }

            for (ct2=0; ct2 < api->paramCount; ct2++) {
                const VarType *vt = &api->params[ct2];
                needFlush += vt->ptrLevel;
                if (vt->ptrLevel && hasInlineDataPointers(api)) {
                    fprintf(f, "    if (%s_length == 0) {\n", vt->name);
                    fprintf(f, "        cmd->%s = NULL;\n", vt->name);
                    fprintf(f, "    } else if (dataSize < io->getMaxInlineSize()) {\n");
                    fprintf(f, "        memcpy(payload, %s, %s_length);\n", vt->name, vt->name);
                    fprintf(f, "        cmd->%s = (", vt->name);
                    printVarType(f, vt);
                    fprintf(f, ")(payload - ((uint8_t *)&cmd[1]));\n");
                    fprintf(f, "        payload += %s_length;\n", vt->name);
                    fprintf(f, "    } else {\n");
                    fprintf(f, "        cmd->%s = %s;\n", vt->name, vt->name);
                    fprintf(f, "    }\n");

                } else {
                    fprintf(f, "    cmd->%s = %s;\n", vt->name, vt->name);
                }
            }
            if (api->ret.typeName[0] || api->sync) {
                needFlush = 1;
            }

            fprintf(f, "    io->coreCommit();\n");
            if (hasInlineDataPointers(api)) {
                fprintf(f, "    if (dataSize >= io->getMaxInlineSize()) {\n");
                fprintf(f, "        io->coreGetReturn(NULL, 0);\n");
                fprintf(f, "    }\n");
            } else if (api->ret.typeName[0]) {
                fprintf(f, "\n    ");
                printVarType(f, &api->ret);
                fprintf(f, " ret;\n");
                fprintf(f, "    io->coreGetReturn(&ret, sizeof(ret));\n");
                fprintf(f, "    return ret;\n");
            } else if (needFlush) {
                fprintf(f, "    io->coreGetReturn(NULL, 0);\n");
            }
        }
        fprintf(f, "};\n\n");


        // Generate a remote sender function
        const char * str = "core";
        if (api->direct) {
            str = "async";
        }

        fprintf(f, "static ");
        printFuncDecl(f, api, "RF_", 0, 0);
        fprintf(f, "\n{\n");
        fprintf(f, "    ThreadIO *io = &((Context *)rsc)->mIO;\n");
        fprintf(f, "    const uint32_t cmdID = RS_CMD_ID_%s;\n", api->name);
        fprintf(f, "    io->%sWrite(&cmdID, sizeof(cmdID));\n\n", str);

        for (ct2=0; ct2 < api->paramCount; ct2++) {
            const VarType *vt = &api->params[ct2];
            if (vt->ptrLevel == 0) {
                fprintf(f, "    io->%sWrite(& %s, sizeof(%s));\n", str, vt->name, vt->name);
            }
        }
        fprintf(f, "\n");

        for (ct2=0; ct2 < api->paramCount; ct2++) {
            const VarType *vt = &api->params[ct2];
            if ((vt->ptrLevel == 1) && (vt->isConst)) {
                fprintf(f, "    io->%sWrite(%s, %s_length);\n", str, vt->name, vt->name);
            }
        }
        fprintf(f, "\n");

        for (ct2=0; ct2 < api->paramCount; ct2++) {
            const VarType *vt = &api->params[ct2];
            if ((vt->ptrLevel == 2) && (vt->isConst)) {
                fprintf(f, "    for (size_t ct = 0; ct < (%s_length_length / sizeof(%s_length)); ct++) {\n", vt->name, vt->name);
                fprintf(f, "        io->%sWrite(%s[ct], %s_length[ct]);\n", str, vt->name, vt->name);
                fprintf(f, "    }\n");
            }
        }
        fprintf(f, "\n");

        for (ct2=0; ct2 < api->paramCount; ct2++) {
            const VarType *vt = &api->params[ct2];
            if ((vt->ptrLevel == 1) && (!vt->isConst)) {
                fprintf(f, "    io->%sGetReturn(%s, %s_length);\n", str, vt->name, vt->name);
            }
        }
        fprintf(f, "\n");

        for (ct2=0; ct2 < api->paramCount; ct2++) {
            const VarType *vt = &api->params[ct2];
            if ((vt->ptrLevel == 2) && (!vt->isConst)) {
                fprintf(f, "    for (size_t ct = 0; ct < (%s_length_length / sizeof(%s_length)); ct++) {\n", vt->name, vt->name);
                fprintf(f, "        io->%sGetReturn(%s[ct], %s_length[ct]);\n", str, vt->name, vt->name);
                fprintf(f, "    }\n");
            }
        }
        fprintf(f, "\n");

        if (api->ret.typeName[0]) {
            fprintf(f, "    ");
            printVarType(f, &api->ret);
            fprintf(f, " retValue;\n");
            fprintf(f, "    io->%sGetReturn(&retValue, sizeof(retValue));\n", str);
            fprintf(f, "    return retValue;\n");
        } else /*if (api->sync)*/ {
            fprintf(f, "    io->%sGetReturn(NULL, 0);\n", str);
        }
        fprintf(f, "}\n\n");
    }

    fprintf(f, "\n");
    fprintf(f, "static RsApiEntrypoints_t s_LocalTable = {\n");
    for (ct=0; ct < apiCount; ct++) {
        fprintf(f, "    LF_%s,\n", apis[ct].name);
    }
    fprintf(f, "};\n");

    fprintf(f, "\n");
    fprintf(f, "static RsApiEntrypoints_t s_RemoteTable = {\n");
    for (ct=0; ct < apiCount; ct++) {
        fprintf(f, "    RF_%s,\n", apis[ct].name);
    }
    fprintf(f, "};\n");

    fprintf(f, "static RsApiEntrypoints_t *s_CurrentTable = &s_LocalTable;\n\n");
    for (ct=0; ct < apiCount; ct++) {
        int needFlush = 0;
        const ApiEntry * api = &apis[ct];

        fprintf(f, "extern \"C\" ");

        printFuncDecl(f, api, "rs", 0, 0);
        fprintf(f, "\n{\n");
        fprintf(f, "    ");
        if (api->ret.typeName[0]) {
            fprintf(f, "return ");
        }
        fprintf(f, "s_CurrentTable->%s(", api->name);

        if (!api->nocontext) {
            fprintf(f, "(Context *)rsc");
        }

        for (ct2=0; ct2 < api->paramCount; ct2++) {
            const VarType *vt = &api->params[ct2];
            if (ct2 > 0 || !api->nocontext) {
                fprintf(f, ", ");
            }
            fprintf(f, "%s", vt->name);
        }
        fprintf(f, ");\n");
        fprintf(f, "}\n\n");
    }

}

void printPlaybackCpp(FILE *f) {
    int ct;
    int ct2;

    fprintf(f, "#include \"rsDevice.h\"\n");
    fprintf(f, "#include \"rsContext.h\"\n");
    fprintf(f, "#include \"rsThreadIO.h\"\n");
    fprintf(f, "#include \"rsgApiStructs.h\"\n");
    fprintf(f, "#include \"rsgApiFuncDecl.h\"\n");
    fprintf(f, "\n");
    fprintf(f, "namespace android {\n");
    fprintf(f, "namespace renderscript {\n");
    fprintf(f, "\n");

    for (ct=0; ct < apiCount; ct++) {
        const ApiEntry * api = &apis[ct];
        int needFlush = 0;

        if (api->direct) {
            continue;
        }

        fprintf(f, "void rsp_%s(Context *con, const void *vp, size_t cmdSizeBytes) {\n", api->name);
        fprintf(f, "    const RS_CMD_%s *cmd = static_cast<const RS_CMD_%s *>(vp);\n", api->name, api->name);

        if (hasInlineDataPointers(api)) {
            fprintf(f, "    const uint8_t *baseData = 0;\n");
            fprintf(f, "    if (cmdSizeBytes != sizeof(RS_CMD_%s)) {\n", api->name);
            fprintf(f, "        baseData = &((const uint8_t *)vp)[sizeof(*cmd)];\n");
            fprintf(f, "    }\n");
        }

        fprintf(f, "    ");
        if (api->ret.typeName[0]) {
            fprintf(f, "\n    ");
            printVarType(f, &api->ret);
            fprintf(f, " ret = ");
        }
        fprintf(f, "rsi_%s(con", api->name);
        for (ct2=0; ct2 < api->paramCount; ct2++) {
            const VarType *vt = &api->params[ct2];
            needFlush += vt->ptrLevel;

            if (hasInlineDataPointers(api) && vt->ptrLevel) {
                fprintf(f, ",\n           cmd->%s_length == 0 ? NULL : (const %s *)&baseData[(intptr_t)cmd->%s]",
                        vt->name, vt->typeName, vt->name);
            } else {
                fprintf(f, ",\n           cmd->%s", vt->name);
            }
        }
        fprintf(f, ");\n");

        if (hasInlineDataPointers(api)) {
            fprintf(f, "    size_t totalSize = 0;\n");
            for (ct2=0; ct2 < api->paramCount; ct2++) {
                if (api->params[ct2].ptrLevel) {
                    fprintf(f, "    totalSize += cmd->%s_length;\n", api->params[ct2].name);
                }
            }

            fprintf(f, "    if ((totalSize != 0) && (cmdSizeBytes == sizeof(RS_CMD_%s))) {\n", api->name);
            fprintf(f, "        con->mIO.coreSetReturn(NULL, 0);\n");
            fprintf(f, "    }\n");
        } else if (api->ret.typeName[0]) {
            fprintf(f, "    con->mIO.coreSetReturn(&ret, sizeof(ret));\n");
        } else if (api->sync || needFlush) {
            fprintf(f, "    con->mIO.coreSetReturn(NULL, 0);\n");
        }

        fprintf(f, "};\n\n");
    }

    for (ct=0; ct < apiCount; ct++) {
        const ApiEntry * api = &apis[ct];
        int needFlush = 0;

        fprintf(f, "void rspr_%s(Context *con, ThreadIO *io) {\n", api->name);
        fprintf(f, "    RS_CMD_%s cmd;\n", api->name);

        for (ct2=0; ct2 < api->paramCount; ct2++) {
            const VarType *vt = &api->params[ct2];
            if (vt->ptrLevel == 0) {
                fprintf(f, "    io->coreRead(&cmd.%s, sizeof(cmd.%s));\n", vt->name, vt->name);
            }
        }
        fprintf(f, "\n");

        for (ct2=0; ct2 < api->paramCount; ct2++) {
            const VarType *vt = &api->params[ct2];
            if (vt->ptrLevel == 1) {
                fprintf(f, "    cmd.%s = (", vt->name);
                printVarType(f, vt);
                fprintf(f, ")malloc(cmd.%s_length);\n", vt->name);

                if (vt->isConst) {
                    fprintf(f, "    if (cmd.%s_length) io->coreRead((void *)cmd.%s, cmd.%s_length);\n", vt->name, vt->name, vt->name);
                }
            }
        }
        fprintf(f, "\n");

        for (ct2=0; ct2 < api->paramCount; ct2++) {
            const VarType *vt = &api->params[ct2];
            if (vt->ptrLevel == 2) {
                fprintf(f, "    for (size_t ct = 0; ct < (cmd.%s_length_length / sizeof(cmd.%s_length)); ct++) {\n", vt->name, vt->name);
                fprintf(f, "        cmd.%s = (", vt->name);
                printVarType(f, vt);
                fprintf(f, ")malloc(cmd.%s_length[ct]);\n", vt->name);
                fprintf(f, "        io->coreRead(& cmd.%s, cmd.%s_length[ct]);\n", vt->name, vt->name);
                fprintf(f, "    }\n");
            }
        }
        fprintf(f, "\n");

        if (api->ret.typeName[0]) {
            fprintf(f, "    ");
            printVarType(f, &api->ret);
            fprintf(f, " ret =\n");
        }

        fprintf(f, "    rsi_%s(", api->name);
        if (!api->nocontext) {
            fprintf(f, "con");
        }
        for (ct2=0; ct2 < api->paramCount; ct2++) {
            const VarType *vt = &api->params[ct2];
            if (ct2 > 0 || !api->nocontext) {
                fprintf(f, ",\n");
            }
            fprintf(f, "           cmd.%s", vt->name);
        }
        fprintf(f, ");\n");

        for (ct2=0; ct2 < api->paramCount; ct2++) {
            const VarType *vt = &api->params[ct2];
            if ((vt->ptrLevel == 1) && (!vt->isConst)) {
                fprintf(f, "    io->coreSetReturn((void *)cmd.%s, cmd.%s_length);\n", vt->name, vt->name);
            }
        }

        for (ct2=0; ct2 < api->paramCount; ct2++) {
            const VarType *vt = &api->params[ct2];
            if ((vt->ptrLevel == 2) && (!vt->isConst)) {
                fprintf(f, "    for (size_t ct = 0; ct < (cmd.%s_length_length / sizeof(cmd.%s_length)); ct++) {\n", vt->name, vt->name);
                fprintf(f, "        io->coreSetReturn((void *)cmd.%s[ct], cmd.%s_length[ct]);\n", vt->name, vt->name);
                fprintf(f, "    }\n");
            }
        }
        fprintf(f, "\n");

        if (api->ret.typeName[0]) {
            fprintf(f, "    io->coreSetReturn(&ret, sizeof(ret));\n");
        } else /*if (needFlush)*/ {
            fprintf(f, "    io->coreSetReturn(NULL, 0);\n");
        }

        for (ct2=0; ct2 < api->paramCount; ct2++) {
            const VarType *vt = &api->params[ct2];
            if (vt->ptrLevel == 1) {
                fprintf(f, "    free((void *)cmd.%s);\n", vt->name);
            }
        }
        for (ct2=0; ct2 < api->paramCount; ct2++) {
            const VarType *vt = &api->params[ct2];
            if (vt->ptrLevel == 2) {
                fprintf(f, "    for (size_t ct = 0; ct < (cmd.%s_length_length / sizeof(cmd.%s_length)); ct++) {\n", vt->name, vt->name);
                fprintf(f, "        free((void *)cmd.%s);\n", vt->name);
                fprintf(f, "    }\n");
            }
        }

        fprintf(f, "};\n\n");
    }

    fprintf(f, "RsPlaybackLocalFunc gPlaybackFuncs[%i] = {\n", apiCount + 1);
    fprintf(f, "    NULL,\n");
    for (ct=0; ct < apiCount; ct++) {
        if (apis[ct].direct) {
            fprintf(f, "    NULL,\n");
        } else {
            fprintf(f, "    %s%s,\n", "rsp_", apis[ct].name);
        }
    }
    fprintf(f, "};\n");

    fprintf(f, "RsPlaybackRemoteFunc gPlaybackRemoteFuncs[%i] = {\n", apiCount + 1);
    fprintf(f, "    NULL,\n");
    for (ct=0; ct < apiCount; ct++) {
        fprintf(f, "    %s%s,\n", "rspr_", apis[ct].name);
    }
    fprintf(f, "};\n");

    fprintf(f, "};\n");
    fprintf(f, "};\n");
}

void yylex();

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s commandFile outFile\n", argv[0]);
        return 1;
    }
    const char* rsgFile = argv[1];
    const char* outFile = argv[2];
    FILE* input = fopen(rsgFile, "r");

    char choice = fgetc(input);
    fclose(input);

    if (choice < '0' || choice > '3') {
        fprintf(stderr, "Uknown command: \'%c\'\n", choice);
        return -2;
    }

    yylex();
    // printf("# of lines = %d\n", num_lines);

    FILE *f = fopen(outFile, "w");

    printFileHeader(f);
    switch (choice) {
        case '0': // rsgApiStructs.h
        {
            fprintf(f, "\n");
            fprintf(f, "#include \"rsContext.h\"\n");
            fprintf(f, "#include \"rsFifo.h\"\n");
            fprintf(f, "\n");
            fprintf(f, "namespace android {\n");
            fprintf(f, "namespace renderscript {\n");
            printStructures(f);
            printFuncDecls(f, "rsi_", 1, 0);
            printPlaybackFuncs(f, "rsp_");
            fprintf(f, "\n\ntypedef struct RsPlaybackRemoteHeaderRec {\n");
            fprintf(f, "    uint32_t command;\n");
            fprintf(f, "    size_t size;\n");
            fprintf(f, "} RsPlaybackRemoteHeader;\n\n");
            fprintf(f, "typedef void (*RsPlaybackLocalFunc)(Context *, const void *, size_t sizeBytes);\n");
            fprintf(f, "typedef void (*RsPlaybackRemoteFunc)(Context *, ThreadIO *);\n");
            fprintf(f, "extern RsPlaybackLocalFunc gPlaybackFuncs[%i];\n", apiCount + 1);
            fprintf(f, "extern RsPlaybackRemoteFunc gPlaybackRemoteFuncs[%i];\n", apiCount + 1);

            fprintf(f, "}\n");
            fprintf(f, "}\n");
        }
        break;

        case '1': // rsgApiFuncDecl.h
        {
            printFuncDecls(f, "rs", 0, 1);
        }
        break;

        case '2': // rsgApi.cpp
        {
            printApiCpp(f);
        }
        break;

        case '3': // rsgApiReplay.cpp
        {
            printFileHeader(f);
            printPlaybackCpp(f);
        }
        break;
    }
    fclose(f);
    return 0;
}
