/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   minishell.h                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: malkilan <malkilan@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/09/15 16:34:08 by malkilan          #+#    #+#             */
/*   Updated: 2026/09/15 18:22:27 by malkilan         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef MINISHELL_H
# define MINISHELL_H

# include "../libft/libft.h"
# include <stdio.h>
# include <stdlib.h>
# include <unistd.h>
# include <string.h>
# include <fcntl.h>
# include <errno.h>
# include <signal.h>
# include <sys/types.h>
# include <sys/wait.h>
# include <sys/stat.h>
# include <dirent.h>
# include <termios.h>
# include <readline/readline.h>
# include <readline/history.h>

# define PROMPT "minishell$ "

# define EXIT_OK          0
# define EXIT_MISUSE      2
# define EXIT_CANT_EXEC   126
# define EXIT_NOT_FOUND   127
# define EXIT_SIG_BASE    128

typedef enum e_quote
{
	Q_NONE = 0,
	Q_SINGLE = 1,
	Q_DOUBLE = 2
}	t_quote;

# define Q_MASK  3
# define Q_BREAK 4

typedef enum e_token_type
{
	T_WORD,
	T_PIPE,
	T_REDIR_IN,
	T_REDIR_OUT,
	T_APPEND,
	T_HEREDOC
}	t_token_type;

typedef struct s_token
{
	t_token_type	type;
	char			*value;
	char			*quotes;
	int				had_quotes;
	struct s_token	*next;
}	t_token;

typedef enum e_redir_type
{
	R_IN,
	R_OUT,
	R_APPEND,
	R_HEREDOC
}	t_redir_type;

typedef struct s_redir
{
	t_redir_type	type;
	char			*filename;
	int				quoted_delim;
	int				heredoc_fd;
	struct s_redir	*next;
}	t_redir;

typedef struct s_cmd
{
	char			**argv;
	t_redir			*redirs;
	struct s_cmd	*next;
}	t_cmd;

typedef struct s_env
{
	char			*key;
	char			*value;
	struct s_env	*next;
}	t_env;

typedef struct s_shell
{
	t_env	*env;
	int		last_status;
	int		stdin_backup;
	int		stdout_backup;
	t_cmd	*current_cmds;
	char	*current_line;
}	t_shell;

extern volatile sig_atomic_t	g_signal;

t_token	*lex(const char *input);
int		expand(t_token *tokens, t_shell *sh);
t_cmd	*parse(t_token *tokens);
void	free_tokens(t_token *tokens);
void	free_cmds(t_cmd *cmds);

char	*expand_word(const char *value, const char *quotes, t_shell *sh);

t_env	*env_init(char **envp);
char	*env_get(t_env *env, const char *key);
int		env_set(t_env **env, const char *key, const char *value);
int		env_unset(t_env **env, const char *key);
char	**env_to_envp(t_env *env);
void	env_free(t_env *env);

int		is_builtin(const char *name);
int		run_builtin(t_cmd *cmd, t_shell *sh);

int		exec_line(t_cmd *cmds, t_shell *sh);

void	signals_setup_interactive(void);
void	signals_setup_child(void);
void	signals_setup_heredoc(void);
void	signals_ignore(void);

void	shell_error(const char *ctx, const char *arg, const char *msg);
int		is_metachar(char c);
int		is_space(char c);
int		line_is_blank(const char *line);
char	*read_input_line(const char *prompt);

#endif
