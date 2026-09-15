/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   parser_utils.c                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: malkilan <malkilan@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/09/02 17:59:24 by malkilan          #+#    #+#             */
/*   Updated: 2026/09/03 02:40:10 by malkilan         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../path_a.h"

t_cmd	*cmd_new(void)
{
	t_cmd	*cmd;

	cmd = malloc(sizeof(t_cmd));
	if (!cmd)
		return (NULL);
	cmd->argv = ft_calloc(1, sizeof(char *));
	if (!cmd->argv)
	{
		free(cmd);
		return (NULL);
	}
	cmd->redirs = NULL;
	cmd->next = NULL;
	return (cmd);
}

/* argv grows by full reallocation each time. A command line has a
** handful of arguments, so the quadratic cost is irrelevant and this
** avoids tracking a capacity field. Takes ownership of value. */
int	cmd_add_arg(t_cmd *cmd, char *value)
{
	char	**grown;
	size_t	n;

	n = 0;
	while (cmd->argv[n])
		n++;
	grown = ft_calloc(n + 2, sizeof(char *));
	if (!grown)
	{
		free(value);
		return (-1);
	}
	ft_memcpy(grown, cmd->argv, n * sizeof(char *));
	grown[n] = value;
	free(cmd->argv);
	cmd->argv = grown;
	return (0);
}

void	free_redirs(t_redir *redirs)
{
	t_redir	*next;

	while (redirs)
	{
		next = redirs->next;
		free(redirs->filename);
		if (redirs->heredoc_fd >= 0)
			close(redirs->heredoc_fd);
		free(redirs);
		redirs = next;
	}
}
