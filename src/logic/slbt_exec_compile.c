/*******************************************************************/
/*  slibtool: a skinny libtool implementation, written in C        */
/*  Copyright (C) 2016  Z. Gilboa                                  */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <string.h>
#include <stdbool.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

#include <slibtool/slibtool.h>
#include "slibtool_spawn_impl.h"

int  slbt_exec_compile(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx)
{
	int			ret;
	int			fdlibs;
	FILE *			fout;
	struct slbt_exec_ctx *	actx = 0;

	/* context */
	if (ectx)
		slbt_reset_placeholders(ectx);
	else if ((ret = slbt_get_exec_ctx(dctx,&ectx)))
		return ret;
	else
		actx = ectx;

	/* .libs directory */
	if (dctx->cctx->drvflags & SLBT_DRIVER_SHARED) {
		if ((fdlibs = open(ectx->ldirname,O_DIRECTORY)) >= 0)
			close(fdlibs);
		else if ((errno != ENOENT) || mkdir(ectx->ldirname,0777)) {
			slbt_free_exec_ctx(actx);
			return -1;
		}
	}

	/* shared library object */
	if (dctx->cctx->drvflags & SLBT_DRIVER_SHARED) {
		if (!(dctx->cctx->drvflags & SLBT_DRIVER_ANTI_PIC)) {
			*ectx->dpic = "-DPIC";
			*ectx->fpic = "-fPIC";
		}

		*ectx->cass = "-c";
		*ectx->lout[0] = "-o";
		*ectx->lout[1] = ectx->lobjname;

		if (!(dctx->cctx->drvflags & SLBT_DRIVER_SILENT)) {
			if (slbt_output_compile(dctx,ectx)) {
				slbt_free_exec_ctx(actx);
				return -1;
			}
		}

		if (((ret = slbt_spawn(ectx,true)) < 0) || ectx->exitcode) {
			slbt_free_exec_ctx(actx);
			return -1;
		}
	}

	/* static archive object */
	if (dctx->cctx->drvflags & SLBT_DRIVER_STATIC) {
		slbt_reset_placeholders(ectx);

		if (dctx->cctx->drvflags & SLBT_DRIVER_PRO_PIC) {
			*ectx->dpic = "-DPIC";
			*ectx->fpic = "-fPIC";
		}

		*ectx->cass = "-c";
		*ectx->lout[0] = "-o";
		*ectx->lout[1] = ectx->aobjname;;

		if (!(dctx->cctx->drvflags & SLBT_DRIVER_SILENT)) {
			if (slbt_output_compile(dctx,ectx)) {
				slbt_free_exec_ctx(actx);
				return -1;
			}
		}

		if (((ret = slbt_spawn(ectx,true)) < 0) || ectx->exitcode) {
			slbt_free_exec_ctx(actx);
			return -1;
		}
	}

	/* libtool object */
	if (!(fout = fopen(ectx->ltobjname,"w"))) {
		slbt_free_exec_ctx(actx);
		return -1;
	}

	ret = fprintf(fout,
		"# slibtool (pre-alphe) generated file\n\n"
		"pic_object='%s'\n"
		"non_pic_object='%s'\n",
		(dctx->cctx->drvflags & SLBT_DRIVER_SHARED)
			? ectx->lobjname
			: "none",
		(dctx->cctx->drvflags & SLBT_DRIVER_STATIC)
			? ectx->aobjname
			: "none");

	fclose(fout);
	slbt_free_exec_ctx(actx);

	return 0;
}
