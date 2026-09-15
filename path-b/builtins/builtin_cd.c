/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   builtin_cd.c                                       :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: malkilan <malkilan@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/27 18:53:29 by malkilan          #+#    #+#             */
/*   Updated: 2026/08/30 00:12:05 by malkilan         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../path_b.h"

/* bash parses options with getopt, which stops at the first operand, so
** only the leading argument can be a flag. We support none, and "-" and
** "--" are not flags: the first means $OLDPWD, the second ends options. */
static int	invalid_option(const char *arg)
{
	char	opt[3];

	if (arg[0] != '-' || !arg[1])
		return (0);
	opt[0] = '-';
	opt[1] = arg[1];
	opt[2] = '\0';
	shell_error("cd", opt, "invalid option");
	ft_putendl_fd("cd: usage: cd [-L|[-P [-e]] [-@]] [dir]", STDERR_FILENO);
	return (1);
}

/* Both directories come from getcwd() rather than $PWD, so a renamed or
** symlinked path resolves the same way for OLDPWD and PWD. */
static void	update_pwd_vars(t_shell *sh, char *old)
{
	char	*cwd;

	if (old)
		env_set(&sh->env, "OLDPWD", old);
	cwd = getcwd(NULL, 0);
	if (cwd)
	{
		env_set(&sh->env, "PWD", cwd);
		free(cwd);
	}
}

/* No argument means $HOME, and "-" means $OLDPWD. Both fail cleanly when
** the variable is unset instead of dereferencing NULL. "cd -" also
** echoes where it landed, which is how bash lets you see the jump. */
static char	*resolve_target(char **argv, t_shell *sh)
{
	char	*var;

	if (!argv[1])
	{
		var = env_get(sh->env, "HOME");
		if (!var)
			shell_error("cd", NULL, "HOME not set");
		return (var);
	}
	if (!ft_strncmp(argv[1], "-", 2))
	{
		var = env_get(sh->env, "OLDPWD");
		if (!var)
			shell_error("cd", NULL, "OLDPWD not set");
		else
			ft_putendl_fd(var, STDOUT_FILENO);
		return (var);
	}
	return (argv[1]);
}

/* Actually walks into target and records the move in $PWD / $OLDPWD.
** Split out of builtin_cd so the option/argument parsing above and the
** noop cases below stay under the norm's per-function line limit. */
static int	cd_perform(char *target, t_shell *sh)
{
	char	*old;

	old = getcwd(NULL, 0);
	if (chdir(target) != 0)
	{
		shell_error("cd", target, strerror(errno));
		free(old);
		return (1);
	}
	update_pwd_vars(sh, old);
	free(old);
	return (EXIT_OK);
}

int	builtin_cd(char **argv, t_shell *sh)
{
	char	*target;

	if (argv[1] && !ft_strncmp(argv[1], "--", 3))
		argv++;
	else if (argv[1] && invalid_option(argv[1]))
		return (EXIT_MISUSE);
	if (argv[1] && argv[2])
	{
		shell_error("cd", NULL, "too many arguments");
		return (1);
	}
	target = resolve_target(argv, sh);
	if (!target)
		return (1);
	if (target[0] == '\0')
		return (EXIT_OK);
	return (cd_perform(target, sh));
}
