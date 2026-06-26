/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ft_strmapi.c                                       :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: malkilan <malkilan@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/08/25 15:31:51 by malkilan          #+#    #+#             */
/*   Updated: 2025/08/25 15:31:51 by malkilan         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "libft.h"

char	*ft_strmapi(char const *s, char (*f)(unsigned int, char))
{
	unsigned int	i;
	int				len;
	char			*str;

	i = 0;
	if (!s || !f)
		return (NULL);
	len = ft_strlen(s);
	str = malloc((len + 1) * sizeof(char));
	if (!str)
		return (NULL);
	while (s[i])
	{
		str[i] = f(i, s[i]);
		i++;
	}
	str[i] = 0;
	return (str);
}

// char my_func(unsigned int i, char c)
// {
//     if (i % 2 == 0 && c >= 'a' && c <= 'z')
//         return (c - 32);  // uppercase
//     return (c);
// }

// int main(void)
// {
//     char *s = "hello world";
//     char *result = ft_strmapi(s, my_func);
//     printf("%s\n", result);  // "HeLlO WoRlD"
//     free(result);
// }