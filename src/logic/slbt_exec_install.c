/*******************************************************************/
/*  slibtool: a skinny libtool implementation, written in C        */
/*  Copyright (C) 2016--2018  Z. Gilboa                            */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

#define ARGV_DRIVER

#include <slibtool/slibtool.h>
#include "slibtool_driver_impl.h"
#include "slibtool_install_impl.h"
#include "slibtool_mapfile_impl.h"
#include "slibtool_readlink_impl.h"
#include "slibtool_spawn_impl.h"
#include "slibtool_symlink_impl.h"
#include "slibtool_errinfo_impl.h"
#include "argv/argv.h"

static int slbt_install_usage(
	int				fdout,
	const char *			program,
	const char *			arg,
	const struct argv_option **	optv,
	struct argv_meta *		meta,
	int				noclr)
{
	char header[512];

	snprintf(header,sizeof(header),
		"Usage: %s --mode=install <install> [options] [SOURCE]... DEST\n"
		"Options:\n",
		program);

	switch (noclr) {
		case 0:
			argv_usage(fdout,header,optv,arg);
			break;

		default:
			argv_usage_plain(fdout,header,optv,arg);
			break;
	}

	argv_free(meta);

	return SLBT_USAGE;
}

static int slbt_exec_install_fail(
	struct slbt_exec_ctx *	actx,
	struct argv_meta *	meta,
	int			ret)
{
	argv_free(meta);
	slbt_free_exec_ctx(actx);
	return ret;
}

static int slbt_exec_install_init_dstdir(
	const struct slbt_driver_ctx *	dctx,
	struct argv_entry *	dest,
	struct argv_entry *	last,
	char *			dstdir)
{
	struct stat	st;
	char *		slash;
	size_t		len;

	(void)dctx;

	if (dest)
		last = dest;

	/* dstdir: initial string */
	if ((size_t)snprintf(dstdir,PATH_MAX,"%s",
			last->arg) >= PATH_MAX)
		return SLBT_BUFFER_ERROR(dctx);

	/* dstdir might end with a slash */
	len = strlen(dstdir);

	if (dstdir[--len] == '/')
		dstdir[len] = 0;

	/* -t DSTDIR? */
	if (dest)
		return 0;

	/* is DEST a directory? */
	if (!(stat(dstdir,&st)))
		if (S_ISDIR(st.st_mode))
			return 0;

	/* remove last path component */
	if ((slash = strrchr(dstdir,'/')))
		*slash = 0;

	return 0;
}

static int slbt_exec_install_import_libraries(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	char *				srcdso,
	char *				dstdir)
{
	char *	host;
	char *	slash;
	char *	dot;
	char *	mark;
	char	srcbuf [PATH_MAX];
	char	implib [PATH_MAX];
	char	hosttag[PATH_MAX];
	char	hostlnk[PATH_MAX];
	char	major  [128];
	char	minor  [128];
	char	rev    [128];

	/* .libs/libfoo.so.x.y.z */
	if ((size_t)snprintf(srcbuf,sizeof(srcbuf),"%s",
			srcdso) >= sizeof(srcbuf))
		return SLBT_BUFFER_ERROR(dctx);

	/* (dso is under .libs) */
	if (!(slash = strrchr(srcbuf,'/')))
		return SLBT_CUSTOM_ERROR(dctx,SLBT_ERR_INSTALL_FLOW);

	/* libfoo.so.x.y.z */
	if ((size_t)snprintf(implib,sizeof(implib),"%s",
			++slash) >= sizeof(implib)
				    - strlen(dctx->cctx->settings.impsuffix))
		return SLBT_BUFFER_ERROR(dctx);

	/* guard against an infinitely long version */
	mark = srcbuf + strlen(srcbuf);

	/* rev */
	if (!(dot = strrchr(srcbuf,'.')))
		return SLBT_CUSTOM_ERROR(dctx,SLBT_ERR_INSTALL_FLOW);
	else if ((size_t)(mark - dot) > sizeof(rev))
		return SLBT_CUSTOM_ERROR(dctx,SLBT_ERR_INSTALL_REV);
	else {
		strcpy(rev,dot);
		*dot = 0;
	}

	/* minor */
	if (!(dot = strrchr(srcbuf,'.')))
		return SLBT_CUSTOM_ERROR(dctx,SLBT_ERR_INSTALL_FLOW);
	else if ((size_t)(mark - dot) > sizeof(minor))
		return SLBT_CUSTOM_ERROR(dctx,SLBT_ERR_INSTALL_REV);
	else {
		strcpy(minor,dot);
		*dot = 0;
	}

	/* major */
	if (!(dot = strrchr(srcbuf,'.')))
		return SLBT_CUSTOM_ERROR(dctx,SLBT_ERR_INSTALL_FLOW);
	else if ((size_t)(mark - dot) > sizeof(major))
		return SLBT_CUSTOM_ERROR(dctx,SLBT_ERR_INSTALL_REV);
	else {
		strcpy(major,dot);
		*dot = 0;
	}

	if (!(dot = strrchr(srcbuf,'.')))
		return SLBT_CUSTOM_ERROR(dctx,SLBT_ERR_INSTALL_FLOW);

	/* .libs/libfoo.so.def.host */
	if ((size_t)snprintf(hostlnk,sizeof(hostlnk),"%s.def.host",
			srcbuf) >= sizeof(hostlnk))
		return SLBT_BUFFER_ERROR(dctx);

	/* libfoo.so.def.{flavor} */
	if (slbt_readlink(hostlnk,hosttag,sizeof(hosttag)))
		return SLBT_SYSTEM_ERROR(dctx,hostlnk);

	/* host/flabor */
	if (!(host = strrchr(hosttag,'.')))
		return SLBT_CUSTOM_ERROR(dctx,SLBT_ERR_INSTALL_FLOW);
	else
		host++;

	/* symlink-based alternate host */
	if (slbt_set_alternate_host(dctx,host,host))
		return SLBT_NESTED_ERROR(dctx);

	/* .libs/libfoo.x.y.z.lib.a */
	sprintf(dot,"%s%s%s%s",
		major,minor,rev,
		dctx->cctx->asettings.impsuffix);

	/* copy: .libs/libfoo.x.y.z.lib.a --> dstdir */
	if (slbt_copy_file(dctx,ectx,srcbuf,dstdir))
		return SLBT_NESTED_ERROR(dctx);

	/* .libs/libfoo.x.lib.a */
	sprintf(dot,"%s%s",
		major,
		dctx->cctx->asettings.impsuffix);

	/* copy: .libs/libfoo.x.lib.a --> dstdir */
	if (slbt_copy_file(dctx,ectx,srcbuf,dstdir))
		return SLBT_NESTED_ERROR(dctx);

	/* /dstdir/libfoo.lib.a */
	strcpy(implib,slash);
	strcpy(dot,dctx->cctx->asettings.impsuffix);

	if ((size_t)snprintf(hostlnk,sizeof(hostlnk),"%s/%s",
			dstdir,slash) >= sizeof(hostlnk))
		return SLBT_BUFFER_ERROR(dctx);

	if (slbt_create_symlink(
			dctx,ectx,
			implib,
			hostlnk,
			false))
		return SLBT_NESTED_ERROR(dctx);

	return 0;
}

static int slbt_exec_install_library_wrapper(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	struct argv_entry *		entry,
	char *				dstdir)
{
	int			fdcwd;
	int			fddst;
	size_t			buflen;
	const char *		base;
	char *			srcline;
	char *			dstline;
	char			clainame[PATH_MAX];
	char			instname[PATH_MAX];
	char			cfgbuf  [PATH_MAX];
	struct slbt_map_info *	mapinfo;

	/* base libfoo.la */
	if ((base = strrchr(entry->arg,'/')))
		base++;
	else
		base = entry->arg;

	/* /dstdir/libfoo.la */
	if ((size_t)snprintf(instname,sizeof(instname),"%s/%s",
			dstdir,base) >= sizeof(instname))
		return SLBT_BUFFER_ERROR(dctx);

	/* libfoo.la.slibtool.install */
	if ((size_t)snprintf(clainame,sizeof(clainame),"%s.slibtool.install",
			entry->arg) >= sizeof(clainame))
		return SLBT_BUFFER_ERROR(dctx);

	/* fdcwd */
	fdcwd = slbt_driver_fdcwd(dctx);

	/* fddst (libfoo.la.slibtool.install, build directory) */
	if ((fddst = openat(fdcwd,clainame,O_RDWR|O_CREAT|O_TRUNC,0644)) < 0)
		return SLBT_SYSTEM_ERROR(dctx,clainame);

	/* mapinfo (libfoo.la, build directory) */
	if (!(mapinfo = slbt_map_file(fdcwd,entry->arg,SLBT_MAP_INPUT))) {
		close(fddst);
		return SLBT_SYSTEM_ERROR(dctx,entry->arg);
	}

	/* srcline */
	if (mapinfo->size < sizeof(cfgbuf)) {
		buflen  = sizeof(cfgbuf);
		srcline = cfgbuf;
	} else {
		buflen  = mapinfo->size;
		srcline = malloc(++buflen);
	}

	if (!srcline) {
		close(fddst);
		slbt_unmap_file(mapinfo);
		return SLBT_SYSTEM_ERROR(dctx,0);
	}

	/* copy config, installed=no --> installed=yes */
	while (mapinfo->mark < mapinfo->cap) {
		if (slbt_mapped_readline(dctx,mapinfo,srcline,buflen) < 0) {
			close(fddst);
			slbt_unmap_file(mapinfo);
			return SLBT_NESTED_ERROR(dctx);
		}

		dstline = strcmp(srcline,"installed=no\n")
			? srcline
			: "installed=yes\n";

		if (slbt_dprintf(fddst,"%s",dstline) < 0) {
			close(fddst);
			slbt_unmap_file(mapinfo);
			return SLBT_SYSTEM_ERROR(dctx,0);
		}
	}

	if (srcline != cfgbuf)
		free(srcline);

	/* close, unmap */
	close(fddst);
	slbt_unmap_file(mapinfo);

	/* cp libfoo.la.slibtool.instal /dstdir/libfoo.la */
	if (slbt_copy_file(dctx,ectx,clainame,instname))
		return SLBT_NESTED_ERROR(dctx);

	return 0;
}

static int slbt_exec_install_entry(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	struct argv_entry *		entry,
	struct argv_entry *		last,
	struct argv_entry *		dest,
	char *				dstdir,
	char **				src,
	char **				dst)
{
	int		ret;
	char *		dot;
	char *		base;
	char *		slash;
	char *		suffix;
	char *		dsosuffix;
	char		sobuf   [64];
	char		target  [PATH_MAX];
	char		srcfile [PATH_MAX];
	char		dstfile [PATH_MAX];
	char		slnkname[PATH_MAX];
	char		dlnkname[PATH_MAX];
	char		lasource[PATH_MAX - 8];
	bool		fexe = false;
	bool		fpe;
	bool		frelease;
	bool		farchive;
	size_t		slen;
	struct stat	st;

	/* executable wrapper? */
	if ((size_t)snprintf(slnkname,sizeof(slnkname),"%s.exe.wrapper",
			entry->arg) >= sizeof(slnkname))
		return SLBT_BUFFER_ERROR(dctx);

	fexe = stat(slnkname,&st)
		? false
		: true;

	dot  = strrchr(entry->arg,'.');

	/* .lai --> .la */
	if (!fexe && dot && !strcmp(dot,".lai"))
		dot[3] = 0;

	/* srcfile */
	if (strlen(entry->arg) + strlen(".libs/") >= (PATH_MAX-1))
		return SLBT_BUFFER_ERROR(dctx);

	strcpy(lasource,entry->arg);

	if ((slash = strrchr(lasource,'/'))) {
		*slash++ = 0;
		sprintf(srcfile,"%s/.libs/%s",lasource,slash);
	} else
		sprintf(srcfile,".libs/%s",lasource);

	/* -shrext, dsosuffix */
	strcpy(sobuf,dctx->cctx->settings.dsosuffix);
	dsosuffix = sobuf;

	if ((size_t)snprintf(slnkname,sizeof(slnkname),"%s.shrext",
			srcfile) >= sizeof(slnkname))
		return SLBT_BUFFER_ERROR(dctx);

	if (!stat(slnkname,&st)) {
		if (slbt_readlink(slnkname,target,sizeof(target)) < 0)
			return SLBT_SYSTEM_ERROR(dctx,slnkname);

		if (strncmp(lasource,target,(slen = strlen(lasource))))
			return SLBT_CUSTOM_ERROR(dctx,SLBT_ERR_INSTALL_FLOW);

		if (strncmp(&target[slen],".shrext",7))
			return SLBT_CUSTOM_ERROR(dctx,SLBT_ERR_INSTALL_FLOW);

		strcpy(sobuf,&target[slen+7]);
	}

	/* executable? ordinary file? */
	if (fexe || !dot || strcmp(dot,".la")) {
		*src = fexe ? srcfile : (char *)entry->arg;
		*dst = dest ? 0 : (char *)last->arg;

		if (!(dctx->cctx->drvflags & SLBT_DRIVER_SILENT))
			if (slbt_output_install(dctx,ectx))
				return SLBT_NESTED_ERROR(dctx);

		return (((ret = slbt_spawn(ectx,true)) < 0) || ectx->exitcode)
			? SLBT_SPAWN_ERROR(dctx) : 0;
	}

	/* legabits? */
	if (dctx->cctx->drvflags & SLBT_DRIVER_LEGABITS)
		if (slbt_exec_install_library_wrapper(dctx,ectx,entry,dstdir))
			return SLBT_NESTED_ERROR(dctx);

	/* *dst: consider: cp libfoo.la /dest/dir/libfoo.la */
	if ((*dst = dest ? 0 : (char *)last->arg))
		if ((dot = strrchr(last->arg,'.')))
			if (!(strcmp(dot,".la")))
				*dst = dstdir;

	/* libfoo.a */
	dot = strrchr(srcfile,'.');
	strcpy(dot,dctx->cctx->settings.arsuffix);

	/* dot/suffix */
	strcpy(slnkname,srcfile);
	dot = strrchr(slnkname,'.');

	/* libfoo.a --> libfoo.so.release */
	sprintf(dot,"%s.release",dsosuffix);
	frelease = stat(slnkname,&st) ? false : true;

	/* libfoo.a --> libfoo.so */
	strcpy(dot,dsosuffix);

	/* libfoo.a installation */
	if (!(dctx->cctx->drvflags & SLBT_DRIVER_DISABLE_STATIC))
		farchive = true;
	else if (slbt_symlink_is_a_placeholder(slnkname))
		farchive = true;
	else
		farchive = false;

	if (farchive)
		if (slbt_copy_file(dctx,ectx,
				srcfile,
				dest ? (char *)dest->arg : *dst))
			return SLBT_NESTED_ERROR(dctx);

	/* PE support: does .libs/libfoo.so.def exist? */
	if ((size_t)snprintf(dstfile,sizeof(dstfile),"%s.def",
			slnkname) >= sizeof(dstfile))
		return SLBT_BUFFER_ERROR(dctx);

	fpe = stat(dstfile,&st) ? false : true;

	/* basename */
	if ((base = strrchr(slnkname,'/')))
		base++;
	else
		base = slnkname;

	/* source (build) symlink target */
	if (slbt_readlink(slnkname,target,sizeof(target)) < 0) {
		/* -all-static? */
		if (slbt_symlink_is_a_placeholder(slnkname))
			return 0;

		/* -avoid-version? */
		if (stat(slnkname,&st))
			return SLBT_SYSTEM_ERROR(dctx,slnkname);

		/* dstfile */
		if ((size_t)snprintf(dstfile,sizeof(dstfile),"%s/%s",
				dstdir,base) >= sizeof(dstfile))
			return SLBT_BUFFER_ERROR(dctx);

		/* single spawn, no symlinks */
		*src = slnkname;
		*dst = dest ? 0 : dstfile;

		if (!(dctx->cctx->drvflags & SLBT_DRIVER_SILENT))
			if (slbt_output_install(dctx,ectx))
				return SLBT_NESTED_ERROR(dctx);

		if (((ret = slbt_spawn(ectx,true)) < 0) || ectx->exitcode)
			return SLBT_SPAWN_ERROR(dctx);

		return 0;
	}

	/* srcfile: .libs/libfoo.so.x.y.z */
	slash = strrchr(srcfile,'/');
	strcpy(++slash,target);

	/* dstfile */
	if (!dest)
		if ((size_t)snprintf(dstfile,sizeof(dstfile),"%s/%s",
				dstdir,target) >= sizeof(dstfile))
			return SLBT_BUFFER_ERROR(dctx);

	/* spawn */
	*src = srcfile;
	*dst = dest ? 0 : dstfile;

	if (!(dctx->cctx->drvflags & SLBT_DRIVER_SILENT))
		if (slbt_output_install(dctx,ectx))
			return SLBT_NESTED_ERROR(dctx);

	if (((ret = slbt_spawn(ectx,true)) < 0) || ectx->exitcode)
		return SLBT_SPAWN_ERROR(dctx);

	/* destination symlink: dstdir/libfoo.so */
	if ((size_t)snprintf(dlnkname,sizeof(dlnkname),"%s/%s",
			dstdir,base) >= sizeof(dlnkname))
		return SLBT_BUFFER_ERROR(dctx);

	/* create symlink: libfoo.so --> libfoo.so.x.y.z */
	if (slbt_create_symlink(
			dctx,ectx,
			target,dlnkname,
			false))
		return SLBT_NESTED_ERROR(dctx);

	if (frelease)
		return 0;

	/* libfoo.so.x --> libfoo.so.x.y.z */
	strcpy(slnkname,target);

	if ((suffix = strrchr(slnkname,'.')))
		*suffix++ = 0;
	else
		return SLBT_CUSTOM_ERROR(dctx,SLBT_ERR_INSTALL_FLOW);

	if ((dot = strrchr(slnkname,'.')))
		*dot++ = 0;
	else
		return SLBT_CUSTOM_ERROR(dctx,SLBT_ERR_INSTALL_FLOW);

	if ((*dot < '0') || (*dot > '9'))
		return SLBT_CUSTOM_ERROR(dctx,SLBT_ERR_INSTALL_FLOW);

	/* libfoo.x.y.z.so? */
	if ((suffix[0] < '0') || (suffix[0] > '9')) {
		if ((dot = strrchr(slnkname,'.')))
			dot++;
		else
			return SLBT_CUSTOM_ERROR(dctx,SLBT_ERR_INSTALL_FLOW);

		if ((*dot < '0') || (*dot > '9'))
			return SLBT_CUSTOM_ERROR(dctx,SLBT_ERR_INSTALL_FLOW);

		for (; *suffix; )
			*dot++ = *suffix++;

		*dot++ = 0;
	}

	/* destination symlink: dstdir/libfoo.so.x */
	if ((size_t)snprintf(dlnkname,sizeof(dlnkname),"%s/%s",
			dstdir,slnkname) >= sizeof(dlnkname))
		return SLBT_BUFFER_ERROR(dctx);

	if (fpe) {
		/* copy: .libs/libfoo.so.x.y.z --> libfoo.so.x */
		if (slbt_copy_file(
				dctx,ectx,
				srcfile,
				dlnkname))
			return SLBT_NESTED_ERROR(dctx);

		/* import libraries */
		if (slbt_exec_install_import_libraries(
				dctx,ectx,
				srcfile,
				dstdir))
			return SLBT_NESTED_ERROR(dctx);
	} else {
		/* create symlink: libfoo.so.x --> libfoo.so.x.y.z */
		if (slbt_create_symlink(
				dctx,ectx,
				target,dlnkname,
				false))
			return SLBT_NESTED_ERROR(dctx);
	}

	return 0;
}

int slbt_exec_install(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx)
{
	int				ret;
	int				fdout;
	char **				argv;
	char **				iargv;
	char **				src;
	char **				dst;
	char *				slash;
	char *				optsh;
	char *				script;
	char *				shtool;
	struct slbt_exec_ctx *		actx;
	struct argv_meta *		meta;
	struct argv_entry *		entry;
	struct argv_entry *		copy;
	struct argv_entry *		dest;
	struct argv_entry *		last;
	const struct argv_option *	optv[SLBT_OPTV_ELEMENTS];
	char				dstdir[PATH_MAX];

	/* dry run */
	if (dctx->cctx->drvflags & SLBT_DRIVER_DRY_RUN)
		return 0;

	/* context */
	if (ectx)
		actx = 0;
	else if ((ret = slbt_get_exec_ctx(dctx,&ectx)))
		return ret;
	else
		actx = ectx;

	/* initial state, install mode skin */
	slbt_reset_arguments(ectx);
	slbt_disable_placeholders(ectx);
	iargv = ectx->cargv;
	fdout = slbt_driver_fdout(dctx);
	optsh = 0;
	script = 0;

	/* work around non-conforming uses of --mode=install */
	if (iargv[1] && (slash = strrchr(iargv[1],'/'))) {
		if (!strcmp(++slash,"install-sh")) {
			optsh  = *iargv++;
			script = *iargv;
		}
	} else {
		slash  = strrchr(iargv[0],'/');
		shtool = slash ? ++slash : iargv[0];
		shtool = strcmp(shtool,"shtool") ? 0 : shtool;

		if (shtool && iargv[1] && !strcmp(iargv[1],"install")) {
			iargv++;
		} else if (shtool) {
			return slbt_install_usage(
				fdout,
				dctx->program,
				0,optv,0,
				dctx->cctx->drvflags & SLBT_DRIVER_ANNOTATE_NEVER);
		}
	}

	/* missing arguments? */
	argv_optv_init(slbt_install_options,optv);

	if (!iargv[1] && (dctx->cctx->drvflags & SLBT_DRIVER_VERBOSITY_USAGE))
		return slbt_install_usage(
			fdout,
			dctx->program,
			0,optv,0,
			dctx->cctx->drvflags & SLBT_DRIVER_ANNOTATE_NEVER);

	/* <install> argv meta */
	if (!(meta = argv_get(
			iargv,optv,
			dctx->cctx->drvflags & SLBT_DRIVER_VERBOSITY_ERRORS
				? ARGV_VERBOSITY_ERRORS
				: ARGV_VERBOSITY_NONE,
			fdout)))
		return slbt_exec_install_fail(
			actx,meta,
			SLBT_CUSTOM_ERROR(dctx,SLBT_ERR_INSTALL_FAIL));

	/* dest, alternate argument vector options */
	argv = ectx->altv;
	copy = meta->entries;
	dest = 0;
	last = 0;

	if (optsh)
		*argv++ = script;

	*argv++ = iargv[0];

	for (entry=meta->entries; entry->fopt || entry->arg; entry++) {
		if (entry->fopt) {
			switch (entry->tag) {
				case TAG_INSTALL_COPY:
					*argv++ = "-c";
					copy = entry;
					break;

				case TAG_INSTALL_FORCE:
					*argv++ = "-f";
					break;

				case TAG_INSTALL_MKDIR:
					*argv++ = "-d";
					copy = 0;
					break;

				case TAG_INSTALL_TARGET_MKDIR:
					*argv++ = "-D";
					copy = 0;
					break;

				case TAG_INSTALL_STRIP:
					*argv++ = "-s";
					break;

				case TAG_INSTALL_PRESERVE:
					*argv++ = "-p";
					break;

				case TAG_INSTALL_USER:
					*argv++ = "-o";
					break;

				case TAG_INSTALL_GROUP:
					*argv++ = "-g";
					break;

				case TAG_INSTALL_MODE:
					*argv++ = "-m";
					break;

				case TAG_INSTALL_DSTDIR:
					*argv++ = "-t";
					dest = entry;
					break;
			}

			if (entry->fval)
				*argv++ = (char *)entry->arg;
		} else
			last = entry;
	}

	/* install */
	if (copy) {
		/* using alternate argument vector */
		if (optsh)
			ectx->altv[0] = optsh;

		ectx->argv    = ectx->altv;
		ectx->program = ectx->altv[0];

		/* marks */
		src = argv++;
		dst = argv++;

		/* dstdir */
		if (slbt_exec_install_init_dstdir(dctx,dest,last,dstdir))
			return slbt_exec_install_fail(
				actx,meta,
				SLBT_NESTED_ERROR(dctx));

		/* install entries one at a time */
		for (entry=meta->entries; entry->fopt || entry->arg; entry++)
			if (!entry->fopt && (dest || (entry != last)))
				if (slbt_exec_install_entry(
						dctx,ectx,
						entry,last,
						dest,dstdir,
						src,dst))
					return slbt_exec_install_fail(
						actx,meta,
						SLBT_NESTED_ERROR(dctx));
	} else {
		/* using original argument vector */
		ectx->argv    = ectx->cargv;
		ectx->program = ectx->cargv[0];

		/* spawn */
		if (!(dctx->cctx->drvflags & SLBT_DRIVER_SILENT))
			if (slbt_output_install(dctx,ectx))
				return SLBT_NESTED_ERROR(dctx);

		if (((ret = slbt_spawn(ectx,true)) < 0) || ectx->exitcode)
			return slbt_exec_install_fail(
				actx,meta,
				SLBT_SPAWN_ERROR(dctx));
	}

	argv_free(meta);
	slbt_free_exec_ctx(actx);

	return 0;
}
