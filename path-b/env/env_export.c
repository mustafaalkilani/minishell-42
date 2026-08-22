/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   env_export.c                                       :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: malkilan <malkilan@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/04 00:00:00 by malkilan          #+#    #+#             */
/*   Updated: 2026/08/04 00:00:00 by malkilan         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../path_b.h"

static char	*make_pair(t_env *node)
{
	char	*head;
	char	*pair;

	head = ft_strjoin(node->key, "=");
	if (!head)
		return (NULL);
	pair = ft_strjoin(head, node->value);
	free(head);
	return (pair);
}

/* Variables declared without a value (`export X`) are skipped: bash
** lists them in export output but does not pass them to children. */
static int	env_export_count(t_env *env)
{
	int	n;

	n = 0;
	while (env)
	{
		if (env->value)
			n++;
		env = env->next;
	}
	return (n);
}

/* Rebuilt on every execve rather than cached, because export/unset can
** change the environment between two commands. */
char	**env_to_envp(t_env *env)
{
	char	**envp;
	int		i;

	envp = ft_calloc((size_t)env_export_count(env) + 1, sizeof(char *));
	if (!envp)
		return (NULL);
	i = 0;
	while (env)
	{
		if (env->value)
		{
			envp[i] = make_pair(env);
			if (!envp[i])
			{
				free_split(envp);
				return (NULL);
			}
			i++;
		}
		env = env->next;
	}
	return (envp);
}

/* Accepts a full "KEY", "KEY=VALUE" or "KEY+=VALUE" string and validates
** only the key part. Identifier rules are the same as C: no leading
** digit, letters, digits and underscore only. */
int	env_key_is_valid(const char *key)
{
	int	i;

	if (!key || !key[0])
		return (0);
	if (!ft_isalpha(key[0]) && key[0] != '_')
		return (0);
	i = 1;
	while (key[i] && key[i] != '=')
	{
		if (key[i] == '+' && key[i + 1] == '=')
			return (1);
		if (!ft_isalnum(key[i]) && key[i] != '_')
			return (0);
		i++;
	}
	return (1);
}

/* KEY+=VALUE keeps whatever KEY already held and adds to it, so the key
** stops one character earlier and the old value seeds the new one. */
int	env_append(t_env **env, char *key, const char *add)
{
	char	*old;
	char	*joined;
	int		rc;

	key[ft_strlen(key) - 1] = '\0';
	old = env_get(*env, key);
	if (!old)
		old = "";
	joined = ft_strjoin(old, add);
	if (!joined)
		return (-1);
	rc = env_set(env, key, joined);
	free(joined);
	return (rc);
}
