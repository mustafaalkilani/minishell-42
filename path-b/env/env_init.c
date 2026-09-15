/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   env_init.c                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: malkilan <malkilan@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/20 12:46:12 by malkilan          #+#    #+#             */
/*   Updated: 2026/08/20 13:37:48 by malkilan         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../path_b.h"

/* value may be NULL, which is how `export X` (declared but unset) is
** represented. Such a variable is listed by export but not exported to
** child processes, matching bash. */
t_env	*env_new(const char *key, const char *value)
{
	t_env	*node;

	node = malloc(sizeof(t_env));
	if (!node)
		return (NULL);
	node->key = ft_strdup(key);
	node->value = NULL;
	if (value)
		node->value = ft_strdup(value);
	if (!node->key || (value && !node->value))
	{
		free(node->key);
		free(node->value);
		free(node);
		return (NULL);
	}
	node->next = NULL;
	return (node);
}

void	env_add_back(t_env **head, t_env *node)
{
	t_env	*cur;

	if (!*head)
	{
		*head = node;
		return ;
	}
	cur = *head;
	while (cur->next)
		cur = cur->next;
	cur->next = node;
}

/* Splits "KEY=VALUE" at the first '='. A missing '=' means the variable
** is declared without a value. */
int	env_set_from_string(t_env **env, const char *assignment)
{
	char	*eq;
	char	*key;
	int		rc;

	eq = ft_strchr(assignment, '=');
	if (!eq)
		return (env_set(env, assignment, NULL));
	key = ft_substr(assignment, 0, (size_t)(eq - assignment));
	if (!key)
		return (-1);
	if (eq != assignment && eq[-1] == '+')
		rc = env_append(env, key, eq + 1);
	else
		rc = env_set(env, key, eq + 1);
	free(key);
	return (rc);
}

/* Every shell records its nesting depth, so a shell started from another
** shell must publish one more than it inherited. Without this, a child
** process sees a stale SHLVL and `env` output diverges from bash. */
static void	env_bump_shlvl(t_env **env)
{
	char	*old;
	char	*new;
	int		level;

	old = env_get(*env, "SHLVL");
	level = 1;
	if (old)
		level = ft_atoi(old) + 1;
	if (level < 1)
		level = 1;
	new = ft_itoa(level);
	if (!new)
		return ;
	env_set(env, "SHLVL", new);
	free(new);
}

t_env	*env_init(char **envp)
{
	t_env	*head;
	int		i;

	head = NULL;
	i = 0;
	while (envp && envp[i])
	{
		if (env_set_from_string(&head, envp[i]) < 0)
		{
			env_free(head);
			return (NULL);
		}
		i++;
	}
	env_bump_shlvl(&head);
	return (head);
}
