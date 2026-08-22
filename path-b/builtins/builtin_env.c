/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   builtin_env.c                                      :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: malkilan <malkilan@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/04 00:00:00 by malkilan          #+#    #+#             */
/*   Updated: 2026/08/04 00:00:00 by malkilan         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../path_b.h"

/* The subject restricts env to no options and no arguments, so anything
** extra is refused rather than trying to emulate `env VAR=x cmd`.
** Variables without a value are skipped, matching real env output. */
int	builtin_env(char **argv, t_shell *sh)
{
	t_env	*cur;

	if (argv[1])
	{
		shell_error("env", argv[1], "No such file or directory");
		return (EXIT_NOT_FOUND);
	}
	cur = sh->env;
	while (cur)
	{
		if (cur->value)
		{
			ft_putstr_fd(cur->key, STDOUT_FILENO);
			ft_putchar_fd('=', STDOUT_FILENO);
			ft_putendl_fd(cur->value, STDOUT_FILENO);
		}
		cur = cur->next;
	}
	return (EXIT_OK);
}
