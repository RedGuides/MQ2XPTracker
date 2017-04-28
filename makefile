!include "../global.mak"

ALL : "$(OUTDIR)\MQ2Xptracker.dll"

CLEAN :
	-@erase "$(INTDIR)\MQ2Xptracker.obj"
	-@erase "$(INTDIR)\vc60.idb"
	-@erase "$(OUTDIR)\MQ2Xptracker.dll"
	-@erase "$(OUTDIR)\MQ2Xptracker.exp"
	-@erase "$(OUTDIR)\MQ2Xptracker.lib"
	-@erase "$(OUTDIR)\MQ2Xptracker.pdb"


LINK32=link.exe
LINK32_FLAGS=kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib $(DETLIB) ..\Release\MQ2Main.lib /nologo /dll /incremental:no /pdb:"$(OUTDIR)\MQ2Xptracker.pdb" /debug /machine:I386 /out:"$(OUTDIR)\MQ2Xptracker.dll" /implib:"$(OUTDIR)\MQ2Xptracker.lib" /OPT:NOICF /OPT:NOREF 
LINK32_OBJS= \
	"$(INTDIR)\MQ2Xptracker.obj" \
	"$(OUTDIR)\MQ2Main.lib"

"$(OUTDIR)\MQ2Xptracker.dll" : "$(OUTDIR)" $(DEF_FILE) $(LINK32_OBJS)
    $(LINK32) $(LINK32_FLAGS) $(LINK32_OBJS)


!IF "$(NO_EXTERNAL_DEPS)" != "1"
!IF EXISTS("MQ2Xptracker.dep")
!INCLUDE "MQ2Xptracker.dep"
!ELSE 
!MESSAGE Warning: cannot find "MQ2Xptracker.dep"
!ENDIF 
!ENDIF 


SOURCE=.\MQ2Xptracker.cpp

"$(INTDIR)\MQ2Xptracker.obj" : $(SOURCE) "$(INTDIR)"

