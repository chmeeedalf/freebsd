# $FreeBSD$

.include <bsd.own.mk>

LIB=	lldbPluginProcessFreeBSD

# include_directories(.)
CFLAGS+=-I${.CURDIR}/../../../contrib/llvm/tools/lldb/source/Plugins/Process/FreeBSD
CFLAGS+=-I${.CURDIR}/../../../contrib/llvm/tools/lldb/source/Plugins/Process/POSIX

SRCDIR=	tools/lldb/source/Plugins/Process/FreeBSD
SRCS=	FreeBSDThread.cpp \
	ProcessFreeBSD.cpp \
	ProcessMonitor.cpp \
	POSIXStopInfo.cpp \
	POSIXThread.cpp \
	ProcessPOSIX.cpp \
	RegisterContextPOSIXProcessMonitor_arm.cpp \
	RegisterContextPOSIXProcessMonitor_arm64.cpp \
	RegisterContextPOSIXProcessMonitor_mips64.cpp \
	RegisterContextPOSIXProcessMonitor_powerpc.cpp \
	RegisterContextPOSIXProcessMonitor_x86.cpp

TGHDRS=	DiagnosticCommonKinds \
	DeclNodes \
	StmtNodes \
	CommentCommandList

.include "../lldb.lib.mk"
