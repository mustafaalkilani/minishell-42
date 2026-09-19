/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   path_b.h                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: rabdalqa <rabdalqa@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/20 10:22:56 by rabdalqa          #+#    #+#             */
/*   Updated: 2026/08/20 15:10:28 by rabdalqa         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef PATH_B_H
# define PATH_B_H

# include "minishell.h"

typedef struct s_pipeline
{
	int		prev_read;
	int		pipefd[2];
	pid_t	last_pid;
}	t_pipeline;

t_env	*env_new(const char *key, const char *value);
void	env_add_back(t_env **head, t_env *node);
t_env	*env_find(t_env *env, const char *key);
int		env_key_is_valid(const char *key);
int		env_set_from_string(t_env **env, const char *assignment);
int		env_append(t_env **env, char *key, const char *add);

int		builtin_echo(char **argv);
int		builtin_cd(char **argv, t_shell *sh);
int		builtin_pwd(void);
int		builtin_env(char **argv, t_shell *sh);
int		builtin_export(char **argv, t_shell *sh);
int		builtin_unset(char **argv, t_shell *sh);
int		builtin_exit(char **argv, t_shell *sh);
int		export_list(t_env *env);

char	*resolve_command(char *cmd, t_shell *sh);
void	free_split(char **split);
void	child_exec(t_cmd *cmd, t_shell *sh);
int		wait_for_all(pid_t last_pid);
int		status_to_exit(int status);

int		apply_redirs(t_cmd *cmd);
int		collect_heredocs(t_cmd *cmds, t_shell *sh);
void	close_other_heredocs(t_cmd *cmds, t_cmd *self);

#endif
