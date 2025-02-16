/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
#pragma once

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define SHELL_TYPE_PERF_LOG (shell_perf_log_get_type ())
G_DECLARE_FINAL_TYPE (ShellPerfLog, shell_perf_log, SHELL, PERF_LOG, GObject)

ShellPerfLog *shell_perf_log_get_default (void);

void shell_perf_log_set_enabled (ShellPerfLog *perf_log,
				 gboolean      enabled);

void shell_perf_log_define_event (ShellPerfLog *perf_log,
				  const char   *name,
				  const char   *description,
				  const char   *signature);
void shell_perf_log_event        (ShellPerfLog *perf_log,
				  const char   *name);
void shell_perf_log_event_i      (ShellPerfLog *perf_log,
				  const char   *name,
				  gint32        arg);
void shell_perf_log_event_x      (ShellPerfLog *perf_log,
				  const char   *name,
				  gint64        arg);
void shell_perf_log_event_s      (ShellPerfLog *perf_log,
				  const char   *name,
				  const char   *arg);

void shell_perf_log_define_statistic (ShellPerfLog *perf_log,
                                      const char   *name,
                                      const char   *description,
                                      const char   *signature);

void shell_perf_log_update_statistic_i (ShellPerfLog *perf_log,
                                        const char   *name,
                                        int           value);
void shell_perf_log_update_statistic_x (ShellPerfLog *perf_log,
                                        const char   *name,
                                        gint64        value);

typedef void (*ShellPerfStatisticsCallback) (ShellPerfLog *perf_log,
                                             gpointer      data);

void shell_perf_log_add_statistics_callback (ShellPerfLog               *perf_log,
                                             ShellPerfStatisticsCallback callback,
                                             gpointer                    user_data,
                                             GDestroyNotify              notify);

void shell_perf_log_collect_statistics (ShellPerfLog *perf_log);

typedef void (*ShellPerfReplayFunction) (gint64      time,
					 const char *name,
					 const char *signature,
					 GValue     *arg,
                                         gpointer    user_data);

void shell_perf_log_replay (ShellPerfLog            *perf_log,
			    ShellPerfReplayFunction  replay_function,
                            gpointer                 user_data);

gboolean shell_perf_log_dump_events (ShellPerfLog   *perf_log,
                                     GOutputStream  *out,
                                     GError        **error);
gboolean shell_perf_log_dump_log    (ShellPerfLog   *perf_log,
                                     GOutputStream  *out,
                                     GError        **error);

G_END_DECLS
