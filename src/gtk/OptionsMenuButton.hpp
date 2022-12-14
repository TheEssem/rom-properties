/***************************************************************************
 * ROM Properties Page shell extension. (GTK+ common)                      *
 * OptionsMenuButton.cpp: Options menu GtkMenuButton container.            *
 *                                                                         *
 * Copyright (c) 2017-2021 by David Korth.                                 *
 * SPDX-License-Identifier: GPL-2.0-or-later                               *
 ***************************************************************************/

#ifndef __ROMPROPERTIES_GTK_OPTIONSMENUBUTTON_HPP__
#define __ROMPROPERTIES_GTK_OPTIONSMENUBUTTON_HPP__

#include "gtk-compat.h"

G_BEGIN_DECLS

#define RP_TYPE_OPTIONS_MENU_BUTTON (rp_options_menu_button_get_type())
#if GTK_CHECK_VERSION(3,0,0)
G_DECLARE_FINAL_TYPE(RpOptionsMenuButton, rp_options_menu_button, RP, OPTIONS_MENU_BUTTON, GtkBox)
#else /* !GTK_CHECK_VERSION(3,0,0) */
G_DECLARE_FINAL_TYPE(RpOptionsMenuButton, rp_options_menu_button, RP, OPTIONS_MENU_BUTTON, GtkHBox)
#endif /* GTK_CHECK_VERSION(3,0,0) */

enum StandardOptionID {
	OPTION_EXPORT_TEXT = -1,
	OPTION_EXPORT_JSON = -2,
	OPTION_COPY_TEXT = -3,
	OPTION_COPY_JSON = -4,
};

/* this function is implemented automatically by the G_DEFINE_TYPE macro */
void		rp_options_menu_button_register_type	(GtkWidget *widget) G_GNUC_INTERNAL;

GtkWidget	*rp_options_menu_button_new		(void) G_GNUC_MALLOC;

GtkArrowType	rp_options_menu_button_get_direction	(RpOptionsMenuButton *widget);
void		rp_options_menu_button_set_direction	(RpOptionsMenuButton *widget,
							 GtkArrowType arrowType);

G_END_DECLS

#ifdef __cplusplus
#include "librpbase/RomData.hpp"

void		rp_options_menu_button_reinit_menu	(RpOptionsMenuButton *widget,
							 const LibRpBase::RomData *romData);

void		rp_options_menu_button_update_op	(RpOptionsMenuButton *widget,
							 int id,
							 const LibRpBase::RomData::RomOp *op);
#endif /* __cplusplus */

#endif /* __ROMPROPERTIES_GTK_OPTIONSMENUBUTTON_HPP__ */
