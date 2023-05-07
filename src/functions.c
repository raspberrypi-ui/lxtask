/* $Id: functions.c 3940 2008-02-10 22:48:45Z nebulon $
 *
 * Copyright (c) 2006 Johannes Zellner, <webmaster@nebulon.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/* modified by Hong Jen Yee <pcman.tw@gmail.com> and the LXDE team
 * on 2008-04-26 to be used in LXDE.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <glib/gi18n.h>
#include <stdio.h>

#include "functions.h"

extern gint refresh_interval;
extern gboolean show_gpu;

static system_status *sys_stat =NULL;

static GList *gpu_stats = NULL;

void read_gpu_status (void)
{
    char *buf = NULL;
    size_t res = 0;
    unsigned long pid, jobs, runtime, active, ts, timestamp, elapsed;
    static unsigned long last_timestamp;
    GList *lptr;
    gpu_status *gpu_data;
    int i, j;
    float max;

    // open the stats file
    FILE *fp = fopen ("/sys/kernel/debug/dri/0/gpu_pid_usage", "rb");
    if (fp == NULL) return;

    // read the stats file a line at a time
    while (getline (&buf, &res, fp) > 0)
    {
        if (sscanf (buf, "timestamp;%ld;", &ts) == 1)
        {
            // use the timestamp line to calculate time since last measurement
            timestamp = ts;
            elapsed = timestamp - last_timestamp;
            last_timestamp = timestamp;
        }
        else if (sscanf (strchr (buf, ';'), ";%ld;%ld;%ld;%ld;", &pid, &jobs, &runtime, &active) == 4)
        {
            // if a PID line is found, check to see if it is already in the list...
            for (lptr = gpu_stats; lptr != NULL; lptr = lptr->next)
            {
                gpu_data = (gpu_status *) lptr->data;
                if (gpu_data->pid == pid) break;
            }

            // ... and add an entry to the list if it is not
            if (gpu_data->pid != pid)
            {
                gpu_data = malloc (sizeof (gpu_status));
                gpu_data->pid = pid;
                for (i = 0; i < 5; i++)
                {
                    gpu_data->load[i] = 0.0;
                    gpu_data->last_val[i] = 0;
                }
                gpu_stats = g_list_append (gpu_stats, gpu_data);
            }

            // depending on which queue is in the line, calculate the percentage of time used since last measurement
            // store the current time value for the next calculation
            i = -1;
            if (!strncmp (buf, "v3d_bin", 7)) i = 0;
            if (!strncmp (buf, "v3d_ren", 7)) i = 1;
            if (!strncmp (buf, "v3d_tfu", 7)) i = 2;
            if (!strncmp (buf, "v3d_csd", 7)) i = 3;
            if (!strncmp (buf, "v3d_cac", 7)) i = 4;

            if (i != -1)
            {
                if (gpu_data->last_val[i] == 0) gpu_data->load[i] = 0.0;
                else
                {
                    gpu_data->load[i] = runtime;
                    gpu_data->load[i] -= gpu_data->last_val[i];
                    gpu_data->load[i] /= elapsed;
                }
                gpu_data->last_val[i] = runtime;
            }

            gpu_data->active = TRUE;
        }
    }

    // clear old values for any pids which did not get updated this time
    for (lptr = gpu_stats; lptr != NULL; lptr = lptr->next)
    {
        gpu_data = (gpu_status *) lptr->data;

        if (!gpu_data->active)
        {
            for (i = 0; i < 5; i++)
            {
                gpu_data->load[i] = 0.0;
                gpu_data->last_val[i] = 0;
            }
        }
        gpu_data->active = FALSE;
    }

    // list is now filled with calculated loadings for each queue for each PID
    free (buf);
    fclose (fp);

    // loop through tasks...
    for (i = 0; i < task_array->len; i++)
    {
        struct task *tmp = &g_array_index (task_array, struct task, i);
        tmp->gpu_percentage = 0;

        // for each task, loop through the list of GPU stats looking for its PID
        for (lptr = gpu_stats; lptr != NULL; lptr = lptr->next)
        {
            gpu_data = (gpu_status *) lptr->data;

            if (gpu_data->pid == tmp->pid)
            {
                // if the PID matches, calculate the max of the five queue values and store in the task array
                max = 0.0;
                for (j = 0; j < 5; j++)
                    if (gpu_data->load[j] > max)
                        max = gpu_data->load[j];
                tmp->gpu_percentage = max * 100.0;
            }
        }
    }
}

float get_gpu_usage (void)
{
    char *buf = NULL;
    size_t res = 0;
    unsigned long jobs, runtime, active, ts, timestamp, elapsed;
    static unsigned long last_timestamp, last_val[5] = {0, 0, 0, 0, 0};
    float max, load[5];
    int i;

    // open the stats file
    FILE *fp = fopen ("/sys/kernel/debug/dri/0/gpu_usage", "rb");
    if (fp == NULL) return 0.0;

    // read the stats file a line at a time
    while (getline (&buf, &res, fp) > 0)
    {
        if (sscanf (buf, "timestamp;%ld;", &ts) == 1)
        {
            // use the timestamp line to calculate time since last measurement
            timestamp = ts;
            elapsed = timestamp - last_timestamp;
            last_timestamp = timestamp;
        }
        else if (sscanf (strchr (buf, ';'), ";%ld;%ld;%ld;", &jobs, &runtime, &active) == 3)
        {
            // depending on which queue is in the line, calculate the percentage of time used since last measurement
            // store the current time value for the next calculation
            i = -1;
            if (!strncmp (buf, "v3d_bin", 7)) i = 0;
            if (!strncmp (buf, "v3d_ren", 7)) i = 1;
            if (!strncmp (buf, "v3d_tfu", 7)) i = 2;
            if (!strncmp (buf, "v3d_csd", 7)) i = 3;
            if (!strncmp (buf, "v3d_cac", 7)) i = 4;

            if (i != -1)
            {
                if (last_val[i] == 0) load[i] = 0.0;
                else
                {
                    load[i] = runtime;
                    load[i] -= last_val[i];
                    load[i] /= elapsed;
                }
                last_val[i] = runtime;
            }
        }
    }

    // list is now filled with calculated loadings for each queue for each PID
    free (buf);
    fclose (fp);

    // calculate the max of the five queue values and store in the task array
    max = 0.0;
    for (i = 0; i < 5; i++)
        if (load[i] > max)
            max = load[i];

    return max;
}

gboolean refresh_task_list(void)
{
    guint i, j;
    GArray *new_task_list;
    gdouble cpu_usage, gpu_usage;
    guint num_cpus;
    guint64 memory_used;
    char tooltip[256];

    if (sys_stat!=NULL)
        num_cpus = sys_stat->cpu_count;
    else
        num_cpus = 1;

    /* gets the new task list */
    new_task_list = (GArray*) get_task_list();

    if (show_gpu) read_gpu_status ();

    /* check if task is new and marks the task that its checked*/
    for(i = 0; i < task_array->len; i++)
    {
        struct task *tmp = &g_array_index(task_array, struct task, i);
        tmp->checked = FALSE;

        for(j = 0; j < new_task_list->len; j++)
        {
            struct task *new_tmp = &g_array_index(new_task_list, struct task, j);

            if(new_tmp->pid == tmp->pid)
            {
                tmp->old_time = tmp->time;

                tmp->time = new_tmp->time;

                new_tmp->time_percentage = (gfloat)(tmp->time - tmp->old_time) * (gfloat)(1000.0f / (refresh_interval * 1000 * num_cpus));
                if(
                    tmp->ppid != new_tmp->ppid ||
                    tmp->state[0]!=new_tmp->state[0] ||
                    tmp->size != new_tmp->size ||
                    tmp->rss != new_tmp->rss ||
                    tmp->time_percentage != new_tmp->time_percentage ||
                    tmp->prio != new_tmp->prio
                 )
                {
                    tmp->ppid = new_tmp->ppid;
                    tmp->state[0]=new_tmp->state[0];
                    tmp->size = new_tmp->size;
                    tmp->rss = new_tmp->rss;
                    tmp->prio = new_tmp->prio;
                    tmp->time_percentage=new_tmp->time_percentage;

                    refresh_list_item(i);
                }
                else
                {
                    tmp->time_percentage=new_tmp->time_percentage;
                }
                tmp->checked = TRUE;
                new_tmp->checked = TRUE;
                break;
            }
            else
                tmp->checked = FALSE;
        }
    }

    /* check for unchecked old-tasks for deleting */
    i = 0;
    while( i < task_array->len)
    {

        struct task *tmp = &g_array_index(task_array, struct task, i);

        if(!tmp->checked)
        {
            remove_list_item(tmp->pid);
            g_array_remove_index(task_array, i);
            tasks--;
        }
        else
            i++;

    }

    /* check for unchecked new tasks for inserting */
    for(i = 0; i < new_task_list->len; i++)
    {
        struct task *new_tmp = &g_array_index(new_task_list, struct task, i);

        if(!new_tmp->checked)
        {
            struct task *new_task = new_tmp;
            g_array_append_val(task_array, *new_task);
            if((show_user_tasks && new_task->uid == own_uid) || (show_root_tasks && new_task->uid == 0) ||  (show_other_tasks && new_task->uid != own_uid && new_task->uid != 0))
                add_new_list_item(tasks);
            tasks++;
        }
    }

    g_array_free(new_task_list, TRUE);

    /* update the CPU and memory progress bars */
    if (sys_stat == NULL)
        sys_stat = g_new0 (system_status, 1);
    get_system_status (sys_stat);

    memory_used = sys_stat->mem_total - sys_stat->mem_free;
    if ( show_cached_as_free )
    {
        memory_used-=sys_stat->mem_cached+sys_stat->mem_buffered;
    }
    /* FIXME: this will work only for < 2048 TB but where so many memory... */
    sprintf (tooltip, _("Memory: %d MB of %d MB used"), (int)(memory_used / 1024),
             (int)(sys_stat->mem_total / 1024));
    if(strcmp(tooltip,gtk_progress_bar_get_text(GTK_PROGRESS_BAR(mem_usage_progress_bar))))
    {
        gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (mem_usage_progress_bar),  (gdouble)memory_used / sys_stat->mem_total);
        gtk_progress_bar_set_text (GTK_PROGRESS_BAR (mem_usage_progress_bar), tooltip);
    }

    cpu_usage = get_cpu_usage (sys_stat);
    sprintf (tooltip,_("CPU usage: %0.0f %%"), cpu_usage * 100.0);
    if(strcmp(tooltip,gtk_progress_bar_get_text(GTK_PROGRESS_BAR(cpu_usage_progress_bar))))
    {
        gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (cpu_usage_progress_bar), cpu_usage);
        gtk_progress_bar_set_text (GTK_PROGRESS_BAR (cpu_usage_progress_bar), tooltip);
    }

    if (show_gpu)
    {
        gpu_usage = get_gpu_usage ();
        sprintf (tooltip,_("GPU usage: %0.0f %%"), gpu_usage * 100.0);
        if(strcmp(tooltip,gtk_progress_bar_get_text(GTK_PROGRESS_BAR(gpu_usage_progress_bar))))
        {
            gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (gpu_usage_progress_bar), gpu_usage);
            gtk_progress_bar_set_text (GTK_PROGRESS_BAR (gpu_usage_progress_bar), tooltip);
        }
    }
    return TRUE;
}

gdouble get_cpu_usage(system_status *sys_stat)
{
    gdouble cpu_usage = 0.0;
    guint current_jiffies;
    guint current_used;
    guint delta_jiffies;

    if ( get_cpu_usage_from_proc( sys_stat ) == FALSE )
    {
        guint i = 0;

        for(i = 0; i < task_array->len; i++)
        {
            struct task *tmp = &g_array_index(task_array, struct task, i);
            cpu_usage += tmp->time_percentage;
        }

        cpu_usage = cpu_usage / (sys_stat->cpu_count * 100.0);
    } else {

        if ( sys_stat->cpu_old_jiffies > 0 ) {
            current_used =
                sys_stat->cpu_user +
                sys_stat->cpu_nice +
                sys_stat->cpu_system;
            current_jiffies =
                current_used +
                sys_stat->cpu_idle;
            delta_jiffies =
                current_jiffies - (gdouble)sys_stat->cpu_old_jiffies;

            cpu_usage = (gdouble)( current_used - sys_stat->cpu_old_used ) /
                     (gdouble)delta_jiffies ;
        }
    }
    return cpu_usage;
}

static gboolean key_file_get_int( GKeyFile* kf, const char* group, const char* name, gboolean def )
{
    int ret;
    GError* err = NULL;
    ret = g_key_file_get_integer( kf, group, name, &err );
    if( err )
    {
        ret = def;
        g_error_free( err );
    }
    return ret;
}

static gboolean key_file_get_bool( GKeyFile* kf, const char* group, const char* name, gboolean def )
{
    return !!key_file_get_int(kf, group, name, def);
}

/*
 * configurationfile support
 */
void load_config(void)
{
    static const char group[]="General";
    GKeyFile *rc_file = g_key_file_new();
    g_key_file_load_from_file(rc_file, config_file, 0, NULL);

    show_user_tasks = key_file_get_bool(rc_file, group, "show_user_tasks", TRUE);
    show_root_tasks = key_file_get_bool(rc_file, group, "show_root_tasks", FALSE);
    show_other_tasks = key_file_get_bool(rc_file, group, "show_other_tasks", FALSE);
    show_full_path = key_file_get_bool(rc_file, group, "show_full_path", FALSE);
    show_cached_as_free = key_file_get_bool(rc_file, group, "show_cached_as_free", TRUE);

    full_view = key_file_get_bool(rc_file, group, "full_view", TRUE);

    win_width = key_file_get_int(rc_file, group, "win_width", 500 );
    win_height = key_file_get_int(rc_file, group, "win_height", 400 );
    refresh_interval = key_file_get_int(rc_file, group, "refresh_interval", 2 );

    g_key_file_free(rc_file);
}

static int check_config(void)
{
	static const char group[]="General";
	int res=0;
    GKeyFile *rc_file = g_key_file_new();
    g_key_file_load_from_file(rc_file, config_file, 0, NULL);
    
    if(show_user_tasks!=key_file_get_bool(rc_file, group, "show_user_tasks", TRUE))
    {
		res=1;
		goto out;
	}
	if(show_root_tasks!=key_file_get_bool(rc_file, group, "show_root_tasks", FALSE))
    {
		res=1;
		goto out;
	}
	if(show_other_tasks!=key_file_get_bool(rc_file, group, "show_other_tasks", FALSE))
    {
		res=1;
		goto out;
	}
	if(show_full_path!=key_file_get_bool(rc_file, group, "show_full_path", FALSE))
	{
		res=1;
		goto out;
	}
	if(show_cached_as_free!=key_file_get_bool(rc_file, group, "show_cached_as_free", TRUE))
	{
		res=1;
		goto out;
	}
	if(full_view!=key_file_get_bool(rc_file, group, "full_view", TRUE))
	{
		res=1;
		goto out;
	}
	gtk_window_get_size(GTK_WINDOW (main_window), &win_width, &win_height);
	if(win_width != key_file_get_int(rc_file, group, "win_width", 500 ))
	{
		res=1;
		goto out;
	}
	if(win_height != key_file_get_int(rc_file, group, "win_height", 400 ))
	{
		res=1;
		goto out;
	}
	if(refresh_interval != key_file_get_int(rc_file, group, "refresh_interval", 2 ))
	{
		res=1;
		goto out;
	}
out:
    g_key_file_free(rc_file);
	return res;
}

void save_config(void)
{
    FILE* rc_file ;
   		
    if(!check_config())
    {
    	return;
    }

    rc_file = fopen( config_file, "w" );
    if(!rc_file) return;

    fputs( "[General]\n", rc_file );
    fprintf( rc_file, "show_user_tasks=%d\n", show_user_tasks);
    fprintf( rc_file, "show_root_tasks=%d\n", show_root_tasks);
    fprintf( rc_file, "show_other_tasks=%d\n", show_other_tasks);
    fprintf( rc_file, "show_full_path=%d\n", show_full_path);

    fprintf( rc_file, "show_cached_as_free=%d\n", show_cached_as_free);

    fprintf( rc_file, "full_view=%d\n", full_view);

    gtk_window_get_size(GTK_WINDOW (main_window), &win_width, &win_height);

    fprintf( rc_file, "win_width=%d\n", win_width);
    fprintf( rc_file, "win_height=%d\n", win_height);
    fprintf( rc_file, "refresh_interval=%d\n", refresh_interval);

    fclose(rc_file);
}
