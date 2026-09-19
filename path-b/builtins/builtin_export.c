/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   builtin_export.c                                   :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: rabdalqa <rabdalqa@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/14 18:47:45 by rabdalqa          #+#    #+#             */
/*   Updated: 2026/08/14 23:06:37 by rabdalqa         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../path_b.h"

static int	invalid_option(const char *arg)
{
	char	opt[3];

	if (arg[0] != '-' || !arg[1])
		return (0);
	opt[0] = '-';
	opt[1] = arg[1];
	opt[2] = '\0';
	shell_error("export", opt, "invalid option");
	ft_putendl_fd("export: usage: export [-fn] [name[=value] ...] or export -p",
		STDERR_FILENO);
	return (1);
}

int	builtin_export(char **argv, t_shell *sh)
{
	int	i;
	int	status;

	if (!argv[1])
		return (export_list(sh->env));
	if (invalid_option(argv[1]))
		return (EXIT_MISUSE);
	i = 1;
	status = EXIT_OK;
	while (argv[i])
	{
		if (!env_key_is_valid(argv[i]))
		{
			shell_error("export", argv[i], "not a valid identifier");
			status = 1;
		}
		else if (env_set_from_string(&sh->env, argv[i]) < 0)
			status = 1;
		i++;
	}
	return (status);
}
