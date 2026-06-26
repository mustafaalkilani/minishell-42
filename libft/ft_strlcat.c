/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ft_strlcat.c                                       :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: malkilan <malkilan@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/08/10 20:00:09 by malkilan          #+#    #+#             */
/*   Updated: 2025/08/12 16:37:26 by malkilan         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "libft.h"

size_t	ft_strlcat(char *dst, const char *src, size_t size)
{
	size_t	i;
	size_t	dst_len;
	size_t	src_len;

	i = 0;
	dst_len = 0;
	src_len = 0;
	while (dst_len < size && dst[dst_len] != 0)
		dst_len++;
	while (src[src_len] != 0)
		src_len++;
	if (dst_len == size)
		return (size + src_len);
	i = 0;
	while (src[i] && dst_len + i < size - 1)
	{
		dst[dst_len + i] = src[i];
		i++;
	}
	dst[dst_len + i] = '\0';
	return (dst_len + src_len);
}

/*
int	main(void)
{
	char src[] = "Hello";
	char dst[] = " World";

	int len = ft_strlcat(dst, src, 12);

	printf("Copied string: '%s'\n", dst);
	printf("Length of src: %d\n", len);

	return (0);
} */
