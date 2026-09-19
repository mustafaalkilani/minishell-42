/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   builtin_dispatch.c                                 :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: rabdalqa <rabdalqa@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/09/15 15:32:41 by rabdalqa          #+#    #+#             */
/*   Updated: 2026/09/15 21:06:27 by rabdalqa         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../path_b.h"

int	is_builtin(const char *name)
{
	if (!name)
		return (0);
	if (!ft_strncmp(name, "echo", 5) || !ft_strncmp(name, "cd", 3))
		return (1);
	if (!ft_strncmp(name, "pwd", 4) || !ft_strncmp(name, "env", 4))
		return (1);
	if (!ft_strncmp(name, "export", 7) || !ft_strncmp(name, "unset", 6))
		return (1);
	if (!ft_strncmp(name, "exit", 5))
		return (1);
	return (0);
}

int	run_builtin(t_cmd *cmd, t_shell *sh)
{
	char	*name;

	name = cmd->argv[0];
	if (!ft_strncmp(name, "echo", 5))
		return (builtin_echo(cmd->argv));
	if (!ft_strncmp(name, "cd", 3))
		return (builtin_cd(cmd->argv, sh));
	if (!ft_strncmp(name, "pwd", 4))
		return (builtin_pwd());
	if (!ft_strncmp(name, "env", 4))
		return (builtin_env(cmd->argv, sh));
	if (!ft_strncmp(name, "export", 7))
		return (builtin_export(cmd->argv, sh));
	if (!ft_strncmp(name, "unset", 6))
		return (builtin_unset(cmd->argv, sh));
	return (builtin_exit(cmd->argv, sh));
}
